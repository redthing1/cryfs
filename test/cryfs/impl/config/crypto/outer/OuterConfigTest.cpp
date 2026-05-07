#include <gtest/gtest.h>
#include <cpp-utils/data/DataFixture.h>
#include <cpp-utils/data/Serializer.h>
#include <cryfs/impl/config/crypto/outer/OuterConfig.h>

using cpputils::Data;
using cpputils::DataFixture;
using cpputils::Serializer;
using boost::none;
using std::ostream;
using namespace cryfs;

// This is needed for google test
namespace boost {
    ostream &operator<<(ostream &stream, const OuterConfig &) {
        return stream << "OuterConfig()";
    }
}
#include <boost/optional/optional_io.hpp>

class OuterConfigTest: public ::testing::Test {
public:
    Data kdfParameters() {
        return DataFixture::generate(128, 2);
    }
};

TEST_F(OuterConfigTest, SomeValues) {
    const Data serialized = OuterConfig{kdfParameters(), DataFixture::generate(1024)}.serialize();
    const OuterConfig deserialized = OuterConfig::deserialize(serialized).value();
    EXPECT_EQ(kdfParameters(), deserialized.kdfParameters);
    EXPECT_EQ(DataFixture::generate(1024), deserialized.encryptedInnerConfig);
}

TEST_F(OuterConfigTest, DataEmpty) {
    const Data serialized = OuterConfig{kdfParameters(), Data(0)}.serialize();
    const OuterConfig deserialized = OuterConfig::deserialize(serialized).value();
    EXPECT_EQ(kdfParameters(), deserialized.kdfParameters);
    EXPECT_EQ(Data(0), deserialized.encryptedInnerConfig);
}

TEST_F(OuterConfigTest, KeyConfigEmpty) {
    const Data serialized = OuterConfig{Data(0), DataFixture::generate(1024)}.serialize();
    const OuterConfig deserialized = OuterConfig::deserialize(serialized).value();
    EXPECT_EQ(Data(0), deserialized.kdfParameters);
    EXPECT_EQ(DataFixture::generate(1024), deserialized.encryptedInnerConfig);
}

TEST_F(OuterConfigTest, DataAndKeyConfigEmpty) {
    const Data serialized = OuterConfig{Data(0), Data(0)}.serialize();
    const OuterConfig deserialized = OuterConfig::deserialize(serialized).value();
    EXPECT_EQ(Data(0), deserialized.kdfParameters);
    EXPECT_EQ(Data(0), deserialized.encryptedInnerConfig);
}

TEST_F(OuterConfigTest, InvalidSerialization) {
    auto deserialized = OuterConfig::deserialize(DataFixture::generate(1024));
    EXPECT_EQ(none, deserialized);
}

TEST_F(OuterConfigTest, OldHeaderIsRejected) {
    Serializer serializer(Serializer::StringSize("cryfs.config;0;scrypt"));
    serializer.writeString("cryfs.config;0;scrypt");
    const auto deserialized = OuterConfig::deserialize(serializer.finished());
    EXPECT_EQ(none, deserialized);
}
