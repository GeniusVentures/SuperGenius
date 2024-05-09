#include "blockchain/impl/key_value_block_storage.hpp"

#include "blockchain/impl/common.hpp"
#include "blockchain/impl/storage_util.hpp"
#include "storage/database_error.hpp"
#include "blockchain/impl/proto/SGBlocks.pb.h"
#include "storage/predefined_keys.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::blockchain,
                            KeyValueBlockStorage::Error,
                            e) {
  using E = sgns::blockchain::KeyValueBlockStorage::Error;
  switch (e) {
    case E::BLOCK_EXISTS:
      return "Block already exists on the chain";
    case E::BODY_DOES_NOT_EXIST:
      return "Block body was not found";
    case E::JUSTIFICATION_DOES_NOT_EXIST:
      return "Justification was not found";
    case E::GENESIS_BLOCK_ALREADY_EXISTS:
      return "Genesis block already exists";
    case E::FINALIZED_BLOCK_NOT_FOUND:
      return "Finalized block not found. Possible storage corrupted";
    case E::GENESIS_BLOCK_NOT_FOUND:
      return "Genesis block not found exists";
  }
  return "Unknown error";
}

namespace sgns::blockchain {
  using primitives::Block;
  using primitives::BlockId;
  using storage::face::MapCursor;
  using storage::face::WriteBatch;
  using Buffer = base::Buffer;
  using Prefix = prefix::Prefix;

  KeyValueBlockStorage::KeyValueBlockStorage(
      std::shared_ptr<crdt::GlobalDB> db,
      std::shared_ptr<crypto::Hasher> hasher,
      std::shared_ptr<KeyValueBlockHeaderRepository> header_repo)
      : db_{std::move(db)},
        hasher_{std::move(hasher)},
        logger_{base::createLogger("Block Storage:")},
        header_repo_{std::move(header_repo)} {}

  outcome::result<std::shared_ptr<KeyValueBlockStorage>>
  KeyValueBlockStorage::create(
      base::Buffer state_root,
      const std::shared_ptr<crdt::GlobalDB> &db,
      const std::shared_ptr<crypto::Hasher> &hasher,
      const std::shared_ptr<KeyValueBlockHeaderRepository> &header_repo,
      const BlockHandler &on_finalized_block_found) {
    auto block_storage = std::make_shared<KeyValueBlockStorage>(
        KeyValueBlockStorage(db, hasher, header_repo));

    auto last_finalized_block_hash_res =
        block_storage->getLastFinalizedBlockHash();

    if (last_finalized_block_hash_res.has_value()) {
      return loadExisting(db, hasher, header_repo, on_finalized_block_found);
    }

    if (last_finalized_block_hash_res
        == outcome::failure(Error::FINALIZED_BLOCK_NOT_FOUND)) {
      return createWithGenesis(
          std::move(state_root), db, hasher, header_repo, on_finalized_block_found);
    }

    return last_finalized_block_hash_res.error();
  }

  outcome::result<std::shared_ptr<KeyValueBlockStorage>>
  KeyValueBlockStorage::loadExisting(
      const std::shared_ptr<crdt::GlobalDB> &db,
      std::shared_ptr<crypto::Hasher> hasher,
      std::shared_ptr<KeyValueBlockHeaderRepository> header_repo,
      const BlockHandler &on_finalized_block_found) {
    auto block_storage = std::make_shared<KeyValueBlockStorage>(
        KeyValueBlockStorage(db, std::move(hasher), header_repo));

    OUTCOME_TRY((auto &&, last_finalized_block_hash),
                block_storage->getLastFinalizedBlockHash());

    OUTCOME_TRY((auto &&, block_header),
                block_storage->getBlockHeader(last_finalized_block_hash));

    primitives::Block finalized_block;
    finalized_block.header = block_header;

    on_finalized_block_found(finalized_block);

    return std::move(block_storage);
  }

  outcome::result<std::shared_ptr<KeyValueBlockStorage>>
  KeyValueBlockStorage::createWithGenesis(
      base::Buffer state_root,
      const std::shared_ptr<crdt::GlobalDB> &db,
      std::shared_ptr<crypto::Hasher> hasher,
      std::shared_ptr<KeyValueBlockHeaderRepository> header_repo,
      const BlockHandler &on_genesis_created) {
    auto block_storage = std::make_shared<KeyValueBlockStorage>(
        KeyValueBlockStorage(db, std::move(hasher), header_repo));

    BOOST_OUTCOME_TRYV2(auto &&, block_storage->ensureGenesisNotExists());

    // state root type is Hash256, however for consistency with spec root hash
    // returns buffer. So we need this conversion
    OUTCOME_TRY((auto &&, state_root_blob),
                base::Hash256::fromSpan(state_root.toVector()));

    auto extrinsics_root_buf = trieRoot({});
    // same reason for conversion as few lines above
    OUTCOME_TRY((auto &&, extrinsics_root),
                base::Hash256::fromSpan(extrinsics_root_buf.toVector()));

    // genesis block initialization
    primitives::Block genesis_block;
    genesis_block.header.number = 0;
    genesis_block.header.extrinsics_root = extrinsics_root;
    genesis_block.header.state_root = state_root_blob;
    // the rest of the fields have default value

    OUTCOME_TRY((auto &&, genesis_block_hash), block_storage->putBlock(genesis_block));
    BOOST_OUTCOME_TRYV2(auto &&, db->Put({std::string((storage::kGenesisBlockHashLookupKey).toString())},
                             Buffer{genesis_block_hash}));
    BOOST_OUTCOME_TRYV2(auto &&, block_storage->setLastFinalizedBlockHash(genesis_block_hash));

    on_genesis_created(genesis_block);
    return std::move(block_storage);
  }

  outcome::result<primitives::BlockHeader> KeyValueBlockStorage::getBlockHeader(
      const primitives::BlockId &id) const {
    return header_repo_->getBlockHeader(id);
  }

  outcome::result<primitives::BlockBody> KeyValueBlockStorage::getBlockBody(
      const primitives::BlockId &id) const {
    OUTCOME_TRY((auto &&, block_data), getBlockData(id));
    if (block_data.body) {
      return block_data.body.value();
    }
    return Error::BODY_DOES_NOT_EXIST;
  }

  outcome::result<primitives::BlockData> KeyValueBlockStorage::getBlockData(
      const primitives::BlockId &id) const {

    OUTCOME_TRY((auto &&, key), idToBufferKey(*db_, id));

    //TODO - For now one block data per block header. Revisit this
    OUTCOME_TRY((auto &&, encoded_block_data), db_->Get({header_repo_->GetHeaderPath()+std::string(key.toString())+ "/tx/0"}));

    
    //OUTCOME_TRY((auto &&, encoded_block_data),
    //            getWithPrefix(*db_, Prefix::BLOCK_DATA, id));
    //OUTCOME_TRY((auto &&, block_data),
    //            scale::decode<primitives::BlockData>(encoded_block_data));
    auto block_data = GetBlockDataFromSerialized(encoded_block_data.toVector());
    return std::move(block_data);
  }

  outcome::result<primitives::Justification>
  KeyValueBlockStorage::getJustification(
      const primitives::BlockId &block) const {
    OUTCOME_TRY((auto &&, block_data), getBlockData(block));
    if (block_data.justification) {
      return block_data.justification.value();
    }
    return Error::JUSTIFICATION_DOES_NOT_EXIST;
  }

  outcome::result<primitives::BlockHash> KeyValueBlockStorage::putBlockHeader(
      const primitives::BlockHeader &header) {
    return header_repo_->putBlockHeader(header);
  }

  outcome::result<void> KeyValueBlockStorage::putBlockData(
      primitives::BlockNumber block_number,
      const primitives::BlockData &block_data) {
    primitives::BlockData to_insert;

    // if block data does not exist, put a new one. Otherwise get the old one
    // and merge with the new one. During the merge new block data fields have
    // higher priority over the old ones (old ones should be rewritten)
    auto existing_block_data_res = getBlockData(block_data.hash);
    if (! existing_block_data_res) {
      to_insert = block_data;
    } else {
      auto existing_data = existing_block_data_res.value();

      // add all the fields from the new block_data
      to_insert.header =
          block_data.header ? block_data.header : existing_data.header;
      to_insert.body = block_data.body ? block_data.body : existing_data.body;
      to_insert.justification = block_data.justification
                                    ? block_data.justification
                                    : existing_data.justification;
      to_insert.message_queue = block_data.message_queue
                                    ? block_data.message_queue
                                    : existing_data.message_queue;
      to_insert.receipt =
          block_data.receipt ? block_data.receipt : existing_data.receipt;
    }

    //OUTCOME_TRY((auto &&, encoded_block_data), scale::encode(to_insert));
    auto encoded_block_data = GetSerializedBlockData(to_insert);
    OUTCOME_TRY((auto &&, id_string), idToStringKey(*db_, block_number));
    //TODO - For now one block data per block header. Revisit this
    BOOST_OUTCOME_TRYV2(auto &&, db_->Put({header_repo_->GetHeaderPath() + id_string + "/tx/0"},Buffer{encoded_block_data}));

    
    //BOOST_OUTCOME_TRYV2(auto &&, putWithPrefix(*db_,
    //                          Prefix::BLOCK_DATA,
    //                          block_number,
    //                          block_data.hash,
    //                          Buffer{encoded_block_data}));
    return outcome::success();
  }

  outcome::result<primitives::BlockHash> KeyValueBlockStorage::putBlock(
      const primitives::Block &block) {
    // TODO(xDimon): Need to implement mechanism for wipe out orphan blocks
    //  (in side-chains whom rejected by finalization)
    //  for avoid leaks of storage space
    auto block_hash = hasher_->blake2b_256(header_repo_->GetHeaderSerializedData(block.header));
    //auto block_in_storage_res =
    //    getWithPrefix(*db_, Prefix::HEADER, block_hash);
    auto block_in_storage_res = header_repo_->getBlockHeader(block_hash);
    if (block_in_storage_res.has_value()) {
      return Error::BLOCK_EXISTS;
    }
    if (block_in_storage_res
        != outcome::failure(blockchain::Error::BLOCK_NOT_FOUND)) {
      return block_in_storage_res.error();
    }

    // insert our block's parts into the database-
    BOOST_OUTCOME_TRYV2(auto &&, putBlockHeader(block.header));

    primitives::BlockData block_data;
    block_data.hash = block_hash;
    block_data.header = block.header;
    block_data.body = block.body;

    BOOST_OUTCOME_TRYV2(auto &&, putBlockData(block.header.number, block_data));
    logger_->info("Added block. Number: {}. Hash: {}. State root: {}",
                  block.header.number,
                  block_hash.toHex(),
                  block.header.state_root.toHex());
    return block_hash;
  }

  outcome::result<void> KeyValueBlockStorage::putJustification(
      const primitives::Justification &j,
      const primitives::BlockHash &hash,
      const primitives::BlockNumber &block_number) {
    // insert justification into the database as a part of BlockData
    // primitives::BlockData block_data{.hash = hash, .justification = j};
     primitives::BlockData block_data;
     block_data.hash = hash;
     block_data.justification = j;
     
    BOOST_OUTCOME_TRYV2(auto &&, putBlockData(block_number, block_data));
    return outcome::success();
  }

  outcome::result<void> KeyValueBlockStorage::removeBlock(
      const primitives::BlockHash &hash,
      const primitives::BlockNumber &number) {
    auto header_rm_res = header_repo_->removeBlockHeader(number);
    if (header_rm_res.has_failure())
    {
      return header_rm_res;
    }

    OUTCOME_TRY((auto &&, key), idToBufferKey(*db_, number));

    //TODO - For now one block data per block header. Revisit this
    return db_->Remove({header_repo_->GetHeaderPath()+std::string(key.toString())+ "/tx/0"});

  }

  outcome::result<primitives::BlockHash>
  KeyValueBlockStorage::getGenesisBlockHash() const {
    auto hash_res = db_->Get({std::string((storage::kGenesisBlockHashLookupKey).toString())});
    if (hash_res.has_value()) {
      primitives::BlockHash hash;
      std::copy(hash_res.value().begin(), hash_res.value().end(), hash.begin());
      return hash;
    }

    if (hash_res == outcome::failure(storage::DatabaseError::NOT_FOUND)) {
      return Error::GENESIS_BLOCK_NOT_FOUND;
    }

    return hash_res.as_failure();
  }

  outcome::result<primitives::BlockHash>
  KeyValueBlockStorage::getLastFinalizedBlockHash() const {
    auto hash_res = db_->Get({std::string((storage::kLastFinalizedBlockHashLookupKey).toString())});
    if (hash_res.has_value()) {
          primitives::BlockHash hash;
    SGBlocks::BlockHashData hash_proto;
    if ( !hash_proto.ParseFromArray( hash_res.value().data(), hash_res.value().size() ) )
    {
        std::cerr << "Failed to parse BlockPayloadData from array." << std::endl;
    }
    hash = (base::Hash256::fromReadableString(hash_proto.hash())).value();
      //std::copy(hash_res.value().begin(), hash_res.value().end(), hash.begin());
      return hash;
    }

    if (hash_res == outcome::failure(storage::DatabaseError::NOT_FOUND)) {
      return Error::FINALIZED_BLOCK_NOT_FOUND;
    }

    return hash_res.as_failure();
  }

  outcome::result<void> KeyValueBlockStorage::setLastFinalizedBlockHash(
      const primitives::BlockHash &hash) {

        SGBlocks::BlockHashData hash_proto;

        hash_proto.set_hash(hash.toReadableString());

        size_t               size = hash_proto.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        hash_proto.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
    BOOST_OUTCOME_TRYV2(auto &&,
        db_->Put({std::string((storage::kLastFinalizedBlockHashLookupKey).toString())}, Buffer{serialized_proto}));

    return outcome::success();
  }

  outcome::result<void> KeyValueBlockStorage::ensureGenesisNotExists() const {
    auto res = getLastFinalizedBlockHash();
    if (res.has_value()) {
      return Error::GENESIS_BLOCK_ALREADY_EXISTS;
    }
    return outcome::success();
  }

  std::vector<uint8_t> KeyValueBlockStorage::GetSerializedBlockData(const primitives::BlockData &block_data)
  {
    SGBlocks::BlockPayloadData data_proto;
    SGBlocks::BlockHeaderData *header_data = data_proto.mutable_header();
    if (block_data.header)
    {
      header_data->set_parent_hash(block_data.header.value().parent_hash.toReadableString());
      header_data->set_block_number(block_data.header.value().number);
      header_data->set_state_root(block_data.header.value().state_root.toReadableString());
      header_data->set_extrinsics_root(block_data.header.value().extrinsics_root.toReadableString());
    }
    data_proto.set_hash(block_data.hash.toReadableString());
    if (block_data.body)
    {
      for (const auto& body_data : block_data.body.value()) 
      {
          data_proto.add_block_body(std::string(body_data.data.toString()));
      }
    }

    size_t               size = data_proto.ByteSizeLong();
    std::vector<uint8_t> serialized_proto( size );

    data_proto.SerializeToArray( serialized_proto.data(), serialized_proto.size() );

    return serialized_proto;
  }
  primitives::BlockData KeyValueBlockStorage::GetBlockDataFromSerialized(const std::vector<uint8_t> &serialized_data) const
  {
    primitives::BlockData block_data;
    SGBlocks::BlockPayloadData data_proto;
    if ( !data_proto.ParseFromArray( serialized_data.data(), serialized_data.size() ) )
    {
        std::cerr << "Failed to parse BlockPayloadData from array." << std::endl;
    }
    primitives::BlockHeader header; 
    header.parent_hash = (base::Hash256::fromReadableString(data_proto.header().parent_hash())).value();
    header.number = data_proto.header().block_number();
    header.state_root = (base::Hash256::fromReadableString(data_proto.header().state_root())).value();
    header.extrinsics_root = (base::Hash256::fromReadableString(data_proto.header().extrinsics_root())).value();
    block_data.header = header;
    block_data.hash = (base::Hash256::fromReadableString(data_proto.hash())).value();

    primitives::BlockBody body;
    for (int i = 0; i < data_proto.block_body_size(); ++i)
    {
      const std::string& tmp = data_proto.block_body(i);
      primitives::Extrinsic curr = {base::Buffer{}.put(tmp)};
      body.push_back(curr);
    }
    block_data.body = body;


    return block_data;
  }
}  // namespace sgns::blockchain
