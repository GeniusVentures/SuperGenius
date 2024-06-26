

#include "crypto/crypto_store/crypto_store_impl.hpp"

#include "crypto/bip39/impl/bip39_provider_impl.hpp"
#include "crypto/ed25519/ed25519_provider_impl.hpp"
#include "crypto/pbkdf2/impl/pbkdf2_provider_impl.hpp"
#include "crypto/random_generator/boost_generator.hpp"
#include "crypto/secp256k1/secp256k1_provider_impl.hpp"
#include "crypto/sr25519/sr25519_provider_impl.hpp"

#include "testutil/outcome.hpp"
#include "testutil/storage/base_fs_test.hpp"

using sgns::base::Blob;
using sgns::base::Buffer;
using sgns::crypto::Bip39Provider;
using sgns::crypto::Bip39ProviderImpl;
using sgns::crypto::BoostRandomGenerator;
using sgns::crypto::CryptoStore;
using sgns::crypto::CryptoStoreError;
using sgns::crypto::CryptoStoreImpl;
using sgns::crypto::ED25519Keypair;
using sgns::crypto::ED25519PrivateKey;
using sgns::crypto::ED25519Provider;
using sgns::crypto::ED25519ProviderImpl;
using sgns::crypto::ED25519PublicKey;
using sgns::crypto::KeyTypeId;
using sgns::crypto::Pbkdf2Provider;
using sgns::crypto::Pbkdf2ProviderImpl;
using sgns::crypto::Secp256k1Provider;
using sgns::crypto::Secp256k1ProviderImpl;
using sgns::crypto::SR25519Keypair;
using sgns::crypto::SR25519Provider;
using sgns::crypto::SR25519ProviderImpl;
using sgns::crypto::SR25519PublicKey;
using sgns::crypto::SR25519SecretKey;

using namespace sgns::crypto::key_types;

static CryptoStoreImpl::Path crypto_store_test_directory =
    boost::filesystem::temp_directory_path() / "crypto_store_test";

struct CryptoStoreTest : public test::FSFixture
{
    CryptoStoreTest() : FSFixture( crypto_store_test_directory )
    {
    }

    void SetUp() override
    {
        auto ed25519_provider   = std::make_shared<ED25519ProviderImpl>();
        auto csprng             = std::make_shared<BoostRandomGenerator>();
        auto sr25519_provider   = std::make_shared<SR25519ProviderImpl>( csprng );
        auto secp256k1_provider = std::make_shared<Secp256k1ProviderImpl>();

        auto pbkdf2_provider = std::make_shared<Pbkdf2ProviderImpl>();
        bip39_provider       = std::make_shared<Bip39ProviderImpl>( std::move( pbkdf2_provider ) );
        crypto_store =
            std::make_shared<CryptoStoreImpl>( std::move( ed25519_provider ), std::move( sr25519_provider ),
                                               std::move( secp256k1_provider ), bip39_provider, std::move( csprng ) );

        mnemonic = "ozone drill grab fiber curtain grace pudding thank cruise elder eight "
                   "picnic";
        EXPECT_OUTCOME_TRUE( e, Buffer::fromHex( "9e885d952ad362caeb4efe34a8e91bd2" ) );
        entropy = std::move( e );
        EXPECT_OUTCOME_TRUE( s, Blob<32>::fromHex( "a4681403ba5b6a3f3bd0b0604ce439a78244"
                                                   "c7d43b127ec35cd8325602dd47fd" ) );
        seed     = s;
        key_type = kProd;

        EXPECT_OUTCOME_TRUE( ed_publ,
                             //        ED25519PublicKey::fromHex("3e765f2bde3daadd443097b3145abf1f71f99f0aa946"
                             //                                  "960990fe02aa26b7fc72"));
                             ED25519PublicKey::fromHex( "3086e3f8cdc1e69f855a1b1907331b7594500c0fc40e"
                                                        "91e25e734513df85289f" ) );
        EXPECT_OUTCOME_TRUE( ed_priv, ED25519PrivateKey::fromHex( "a4681403ba5b6a3f3bd0b0604ce439a78244c7d43b1"
                                                                  "27ec35cd8325602dd47fd" ) );
        ed_pair = { ed_priv, ed_publ };

        EXPECT_OUTCOME_TRUE(
            sr_publ, SR25519PublicKey::fromHex( "7e68abebffcfe3409af87fbb0b5254b7add6ec3efc1f5b882f3621df45afec15" ) );
        EXPECT_OUTCOME_TRUE(
            sr_secr, SR25519SecretKey::fromHex( "d0c92c34038c17ea76208b04623203cbd9ce0901e82486da0053c0c2c4488b69"
                                                "19f52f334750e98500ba84fd528a6ff810b91b43d11e3d3eb1c2176c9dda0b57" ) );
        sr_pair = { sr_secr, sr_publ };

        EXPECT_OUTCOME_TRUE_MSG_1( crypto_store->initialize( crypto_store_test_directory ), "initialization failed" );
    }

    bool isStoredOnDisk( KeyTypeId kt, const Blob<32> &public_key )
    {
        auto file_name = sgns::crypto::decodeKeyTypeId( kt ) + public_key.toHex();
        auto file_path = crypto_store_test_directory / file_name;
        return boost::filesystem::exists( file_path );
    }

    std::shared_ptr<Bip39Provider>   bip39_provider;
    std::shared_ptr<CryptoStoreImpl> crypto_store;
    std::string                      mnemonic;
    Buffer                           entropy;
    Blob<32>                         seed;
    KeyTypeId                        key_type;

    ED25519Keypair ed_pair;
    SR25519Keypair sr_pair;
};

/**
 * @given cryptostore instance, type, mnemonic and predefined key pair
 * @when generateEd25519Keypair is called
 * @then method call succeeds and result matches predefined key pair
 * @and generated key pair is stored in memory
 */
TEST_F(CryptoStoreTest, generateEd25519KeypairMnemonicSuccess) {
  EXPECT_OUTCOME_FALSE(
      err, crypto_store->findEd25519Keypair(key_type, ed_pair.public_key));
  ASSERT_EQ(err, CryptoStoreError::KEY_NOT_FOUND);

  EXPECT_OUTCOME_TRUE(pair,
                      crypto_store->generateEd25519Keypair(key_type, mnemonic));
  ASSERT_EQ(pair, ed_pair);

  // check that created pair is now contained in memory
  EXPECT_OUTCOME_TRUE(
      found, crypto_store->findEd25519Keypair(key_type, pair.public_key));
  ASSERT_EQ(found, pair);

  // not stored on disk
  ASSERT_FALSE(isStoredOnDisk(key_type, pair.public_key));
}

/**
 * @given cryptostore instance, type, mnemonic and predefined key pair
 * @when generateSr25519Keypair is called
 * @then method call succeeds and result matches predefined key pair
 * @and generated key pair is stored in memory
 */
TEST_F(CryptoStoreTest, generateSr25519KeypairMnemonicSuccess) {
  EXPECT_OUTCOME_FALSE(
      err, crypto_store->findSr25519Keypair(key_type, ed_pair.public_key));
  ASSERT_EQ(err, CryptoStoreError::KEY_NOT_FOUND);

  EXPECT_OUTCOME_TRUE(pair,
                      crypto_store->generateSr25519Keypair(key_type, mnemonic));
  ASSERT_EQ(pair, sr_pair);

  // check that created pair is now contained in memory
  EXPECT_OUTCOME_TRUE(
      found, crypto_store->findSr25519Keypair(key_type, pair.public_key));
  ASSERT_EQ(found, pair);
  // not stored on disk
  ASSERT_FALSE(isStoredOnDisk(key_type, pair.public_key));
}

/**
 * @given cryptostore instance, type, seed and predefined key pair
 * @when generateEd25519Keypair is called
 * @then method call succeeds and result matches predefined key pair
 * @and generated key pair is stored in memory
 */
TEST_F(CryptoStoreTest, generateEd25519KeypairSeedSuccess) {
  EXPECT_OUTCOME_FALSE(
      err, crypto_store->findEd25519Keypair(key_type, ed_pair.public_key));
  ASSERT_EQ(err, CryptoStoreError::KEY_NOT_FOUND);

  auto &&pair = crypto_store->generateEd25519Keypair(key_type, seed);
  ASSERT_EQ(pair, ed_pair);

  // check that created pair is now contained in memory
  EXPECT_OUTCOME_TRUE(
      found, crypto_store->findEd25519Keypair(key_type, pair.public_key));
  ASSERT_EQ(found, pair);
  // not stored on disk
  ASSERT_FALSE(isStoredOnDisk(key_type, pair.public_key));
}

/**
 * @given cryptostore instance, type, seed and predefined key pair
 * @when generateSr25519Keypair is called
 * @then method call succeeds and result matches predefined key pair
 * @and key generated pair is stored in memory
 */
TEST_F(CryptoStoreTest, generateSr25519KeypairSeedSuccess) {
  EXPECT_OUTCOME_FALSE(
      err, crypto_store->findSr25519Keypair(key_type, sr_pair.public_key));
  ASSERT_EQ(err, CryptoStoreError::KEY_NOT_FOUND);

  auto &&pair = crypto_store->generateSr25519Keypair(key_type, seed);
  ASSERT_EQ(pair, sr_pair);

  // check that created pair is now contained in memory
  EXPECT_OUTCOME_TRUE(
      found, crypto_store->findSr25519Keypair(key_type, pair.public_key));
  ASSERT_EQ(found, pair);

  // not stored on disk
  ASSERT_FALSE(isStoredOnDisk(key_type, pair.public_key));
}

/**
 * @given cryptostore instance, and key type
 * @when call generateEd25519Keypair(key_type)
 * @then a new ed25519 key pair is generated and stored on disk
 */
TEST_F(CryptoStoreTest, generateEd25519KeypairStoreSuccess) {
  EXPECT_OUTCOME_TRUE(pair, crypto_store->generateEd25519Keypair(key_type));

  // check that created pair is contained in the storage on disk
  EXPECT_OUTCOME_TRUE(
      found, crypto_store->findEd25519Keypair(key_type, pair.public_key));
  ASSERT_EQ(found, pair);

  // stored on disk
  ASSERT_TRUE(isStoredOnDisk(key_type, pair.public_key));
}

/**
 * @given cryptostore instance, and key type
 * @when call generateSr25519Keypair(key_type)
 * @then a new ed25519 key pair is generated and stored on disk
 */
TEST_F(CryptoStoreTest, generateSr25519KeypairStoreSuccess) {
  EXPECT_OUTCOME_TRUE(pair, crypto_store->generateSr25519Keypair(key_type));

  // check that created pair is contained in the storage on disk
  EXPECT_OUTCOME_TRUE(
      found, crypto_store->findSr25519Keypair(key_type, pair.public_key));
  ASSERT_EQ(found, pair);

  // stored on disk
  ASSERT_TRUE(isStoredOnDisk(key_type, pair.public_key));
}

/**
 * @given cryptostore instance, and key type
 * @when call getEd25519PublicKeys
 * @then collection of all ed25519 public keys of provided type is returned
 */
TEST_F(CryptoStoreTest, getEd25519PublicKeysSuccess) {
  EXPECT_OUTCOME_TRUE(pair1, crypto_store->generateEd25519Keypair(kProd));
  EXPECT_OUTCOME_TRUE(pair2, crypto_store->generateEd25519Keypair(kProd));
  EXPECT_OUTCOME_SUCCESS(pair3, crypto_store->generateEd25519Keypair(kLp2p));
  EXPECT_OUTCOME_SUCCESS(pair4, crypto_store->generateSr25519Keypair(kProd));
  EXPECT_OUTCOME_SUCCESS(pair5, crypto_store->generateSr25519Keypair(kAcco));

  std::set<ED25519PublicKey> ed_production_keys_set = {pair1.public_key,
                                                 pair2.public_key};
  std::vector<ED25519PublicKey> ed_production_keys(ed_production_keys_set.begin(),
                                             ed_production_keys_set.end());

  auto &&keys = crypto_store->getEd25519PublicKeys(kProd);
  ASSERT_EQ(ed_production_keys, keys);
}

/**
 * @given cryptostore instance, and key type
 * @when call getSr25519PublicKeys
 * @then collection of all sr25519 public keys of provided type is returned
 */
TEST_F(CryptoStoreTest, getSr25519PublicKeysSuccess) {
  EXPECT_OUTCOME_TRUE(pair1, crypto_store->generateSr25519Keypair(kProd));
  EXPECT_OUTCOME_TRUE(pair2, crypto_store->generateSr25519Keypair(kProd));
  EXPECT_OUTCOME_SUCCESS(pair3, crypto_store->generateSr25519Keypair(kLp2p));
  EXPECT_OUTCOME_SUCCESS(pair4, crypto_store->generateEd25519Keypair(kProd));
  EXPECT_OUTCOME_SUCCESS(pair5, crypto_store->generateEd25519Keypair(kAcco));

  std::set<SR25519PublicKey> sr_production_keys_set = {pair1.public_key,
                                                 pair2.public_key};
  std::vector<SR25519PublicKey> sr_production_keys(sr_production_keys_set.begin(),
                                             sr_production_keys_set.end());

  auto &&keys = crypto_store->getSr25519PublicKeys(kProd);
  ASSERT_EQ(sr_production_keys, keys);
}
