#include "OuterConfig.h"

using std::string;
using std::exception;
using cpputils::Data;
using cpputils::Serializer;
using cpputils::Deserializer;
using boost::optional;
using boost::none;
using namespace cpputils::logging;

namespace cryfs {
    const string OuterConfig::HEADER = "cryfs.config;1;scrypt";

    void OuterConfig::_checkHeader(Deserializer *deserializer) {
        const string header = deserializer->readString();
        if (header != HEADER) {
            throw std::runtime_error("Invalid header");
        }
    }

    void OuterConfig::_writeHeader(Serializer *serializer) {
        serializer->writeString(HEADER);
    }

    Data OuterConfig::serialize() const {
        try {
            Serializer serializer(Serializer::StringSize(HEADER)
                                  + Serializer::DataSize(kdfParameters)
                                  + encryptedInnerConfig.size());
            _writeHeader(&serializer);
            serializer.writeData(kdfParameters);
            serializer.writeTailData(encryptedInnerConfig);
            return serializer.finished();
        } catch (const exception &e) {
            LOG(ERR, "Error serializing CryConfigEncryptor: {}", e.what());
            throw; // This is a programming logic error. Pass through exception.
        }
    }

    optional<OuterConfig> OuterConfig::deserialize(const Data &data) {
        Deserializer deserializer(&data);
        try {
            _checkHeader(&deserializer);
            return _deserializeNewFormat(&deserializer);
        } catch (const exception &e) {
            LOG(ERR, "Error deserializing outer configuration: {}", e.what());
            return none; // This can be caused by invalid input data and does not have to be a programming error. Don't throw exception.
        }
    }

    OuterConfig OuterConfig::_deserializeNewFormat(Deserializer *deserializer) {
        auto kdfParameters = deserializer->readData();
        auto encryptedInnerConfig = deserializer->readTailData();
        deserializer->finished();
        return OuterConfig {std::move(kdfParameters), std::move(encryptedInnerConfig)};
    }
}
