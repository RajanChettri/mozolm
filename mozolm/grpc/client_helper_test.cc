// Copyright 2021 MozoLM Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mozolm/grpc/client_helper.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mozolm/stubs/logging.h"
#include "google/protobuf/text_format.h"
#include "gmock/gmock.h"
#include "mozolm/stubs/status-matchers.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "mozolm/grpc/server_config.pb.h"
#include "mozolm/grpc/server_helper.h"
#include "mozolm/models/model_config.pb.h"
#include "mozolm/models/ppm_as_fst_options.pb.h"
#include "mozolm/utils/file_util.h"

namespace mozolm {
namespace grpc {
namespace {

const char kModelsTestDir[] =
    "mozolm/models/testdata";
const char kCharFstModelFilename[] = "gutenberg_en_char_ngram_o2_kn.fst";
const char kPpmAsFstModelFilename[] = "gutenberg_praise_of_folly.txt";
const int kDefaultTimeoutSec = 2;  // Default client timeout (in seconds).

// Model information: type and adaptivity.
using ModelInfo = std::pair<ModelConfig::ModelType, bool>;
using ModelParams = std::vector<ModelInfo>;

class ClientHelperTest : public ::testing::TestWithParam<ModelParams>  {
 protected:
  void SetUp() override {
    // Initialize and start the local server.
    MakeServerConfig(GetParam());
    EXPECT_OK(server_.Init(server_config_)) << "Failed to initialize server";
    EXPECT_OK(server_.Run(/* wait_till_terminated= */false));

    // Set up initial client configuration.
    const int server_port = server_.server().selected_port();
    EXPECT_LT(0, server_port) << "Invalid port: " << server_port;
    *client_config_.mutable_server() = server_config_;
    InitConfigDefaults(&client_config_);
    client_config_.set_timeout_sec(kDefaultTimeoutSec);
    client_config_.mutable_server()->set_address_uri(
        absl::StrCat("localhost:", server_port));
  }

  // Initializes server configuration.
  void MakeServerConfig(const ModelParams &model_params) {
    const std::string config_contents = R"(
        address_uri: "localhost:0"
        auth { credential_type:CREDENTIAL_INSECURE }
        wait_for_clients: false)";
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(config_contents,
                                                    &server_config_));
    auto *model_hub = server_config_.mutable_model_hub_config();
    if (model_params.size() > 1) {
      model_hub->set_mixture_type(ModelHubConfig::INTERPOLATION);
    }
    for (const auto &model_info : model_params) {
      EXPECT_TRUE(MakeModelConfig(model_info, model_hub->add_model_config()))
          << "Failed to configure model: " << model_info.first;
    }
  }

 private:
  // Returns full path to the model.
  std::string ModelPath(std::string_view model_dir,
                        std::string_view model_filename) const {
    const std::filesystem::path model_path = (
        std::filesystem::current_path() /
        model_dir / model_filename).make_preferred();
    return model_path.string();
  }

  // Makes a single simple model configuration given basic parameters.
  bool MakeModelConfig(ModelInfo model_info, ModelConfig *model) const {
    const ModelConfig::ModelType model_type = model_info.first;
    model->set_type(model_type);
    std::string model_path;
    if (model_type == ModelConfig::SIMPLE_CHAR_BIGRAM) {
      // Simple character bigram can get by without data, in which case it will
      // resort to uniform distribution estimates.
    } else if (model_type == ModelConfig::CHAR_NGRAM_FST) {
      model_path = ModelPath(kModelsTestDir, kCharFstModelFilename);
    } else if (model_type == ModelConfig::PPM_AS_FST) {
      model_path = ModelPath(kModelsTestDir, kPpmAsFstModelFilename);
      auto *options = model->mutable_storage()->mutable_ppm_options();
      options->set_max_order(2);  // Bigrams.
      options->set_static_model(model_info.second);
    } else {
      GOOGLE_LOG(ERROR) << "Unsupported model_type: " << model_type;
      return false;
    }
    if (!model_path.empty()) {
      model->mutable_storage()->set_model_file(model_path);
    }
    return true;
  }

 protected:
  // Client configuration.
  ClientConfig client_config_;

  // Server configuration.
  ServerConfig server_config_;

  // Actual in-process server to speak to.
  ServerHelper server_;
};

TEST(ClientHelperConfigTest, CheckConfig) {
  ClientConfig config;
  InitConfigDefaults(&config);
  const ServerConfig &server_config = config.server();
  EXPECT_EQ(kDefaultServerAddress, server_config.address_uri());
  EXPECT_TRUE(server_config.wait_for_clients());
  EXPECT_LT(0, config.timeout_sec());
}

TEST_P(ClientHelperTest, CheckOneKbestSample) {
  std::unique_ptr<ClientHelper> client = absl::make_unique<ClientHelper>(
      client_config_);
  constexpr int kBest = 10;
  std::string result;
  EXPECT_OK(client->OneKbestSample(kBest, /* context_string= */"", &result));
  EXPECT_FALSE(result.empty());
}

TEST_P(ClientHelperTest, CheckRandGen) {
  std::unique_ptr<ClientHelper> client = absl::make_unique<ClientHelper>(
      client_config_);
  const int kNumIterations = 5;
  std::string context = "";
  std::string result;
  for (int i = 0; i < kNumIterations; ++i) {
    EXPECT_OK(client->RandGen(context, &result));
    EXPECT_FALSE(result.empty());
    context += result;
  }
}

TEST_P(ClientHelperTest, CheckCalcBitsPerCharacter) {
  // Mock test file.
  const auto temp_text_status = file::WriteTempTextFile(
      "test.txt", "Hello world!");
  EXPECT_OK(temp_text_status.status());
  const std::string test_file = temp_text_status.value();
  EXPECT_FALSE(test_file.empty());

  // Calculate entropy.
  std::unique_ptr<ClientHelper> client = absl::make_unique<ClientHelper>(
      client_config_);
  std::string result;
  EXPECT_OK(client->CalcBitsPerCharacter(test_file, &result));
  EXPECT_FALSE(result.empty());

  // Cleanup.
  EXPECT_TRUE(std::filesystem::remove(test_file));
}

INSTANTIATE_TEST_SUITE_P(
    ClientServerMiniEnd2End, ClientHelperTest, ::testing::Values(
        // Static character bigram.
        ModelParams({{ModelConfig::SIMPLE_CHAR_BIGRAM, /* adaptive= */false}}),
        // Static character n-gram.
        ModelParams({{ModelConfig::CHAR_NGRAM_FST, /* adaptive= */false}}),
        // Static PPM.
        ModelParams({{ModelConfig::PPM_AS_FST, /* adaptive= */false}}),
        // Adaptive PPM.
        ModelParams({{ModelConfig::PPM_AS_FST, /* adaptive= */true}}),
        // Interpolated static PPM and static character n-gram.
        ModelParams({{ModelConfig::PPM_AS_FST, /* adaptive= */false},
                     {ModelConfig::CHAR_NGRAM_FST, /* adaptive= */false}}),
        // Interpolated adaptive PPM and static character n-gram.
        ModelParams({{ModelConfig::PPM_AS_FST, /* adaptive= */true},
                     {ModelConfig::CHAR_NGRAM_FST, /* adaptive= */false}})));

}  // namespace
}  // namespace grpc
}  // namespace mozolm