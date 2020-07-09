#include <lib/blocks.hpp>
#include <lib/memory.hpp>
#include <lib/work.hpp>
#include "common.hpp"
// #include "election.hpp"
#include "wallet.hpp"
#include <secure/buffer.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/variant/get.hpp>

#include <numeric>

std::bitset<16> constexpr sgns::message_header::block_type_mask;
std::bitset<16> constexpr sgns::message_header::count_mask;

std::chrono::seconds constexpr sgns::telemetry_cache_cutoffs::test;
std::chrono::seconds constexpr sgns::telemetry_cache_cutoffs::beta;
std::chrono::seconds constexpr sgns::telemetry_cache_cutoffs::live;

namespace
{
sgns::protocol_constants const & get_protocol_constants ()
{
	static sgns::network_params params;
	return params.protocol;
}
}

uint64_t sgns::ip_address_hash_raw (boost::asio::ip::address const & ip_a, uint16_t port)
{
	static sgns::random_constants constants;
	debug_assert (ip_a.is_v6 ());
	uint64_t result;
	sgns::uint128_union address;
	address.bytes = ip_a.to_v6 ().to_bytes ();
	blake2b_state state;
	blake2b_init (&state, sizeof (result));
	blake2b_update (&state, constants.random_128.bytes.data (), constants.random_128.bytes.size ());
	if (port != 0)
	{
		blake2b_update (&state, &port, sizeof (port));
	}
	blake2b_update (&state, address.bytes.data (), address.bytes.size ());
	blake2b_final (&state, &result, sizeof (result));
	return result;
}

sgns::message_header::message_header (sgns::message_type type_a) :
version_max (get_protocol_constants ().protocol_version),
version_using (get_protocol_constants ().protocol_version),
type (type_a)
{
}

sgns::message_header::message_header (bool & error_a, sgns::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void sgns::message_header::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	static sgns::network_params network_params;
	sgns::write (stream_a, network_params.header_magic_number);
	sgns::write (stream_a, version_max);
	sgns::write (stream_a, version_using);
	sgns::write (stream_a, get_protocol_constants ().protocol_version_min (use_epoch_2_min_version_a));
	sgns::write (stream_a, type);
	sgns::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool sgns::message_header::deserialize (sgns::stream & stream_a)
{
	auto error (false);
	try
	{
		static sgns::network_params network_params;
		uint16_t extensions_l;
		std::array<uint8_t, 2> magic_number_l;
		read (stream_a, magic_number_l);
		if (magic_number_l != network_params.header_magic_number)
		{
			throw std::runtime_error ("Magic numbers do not match");
		}

		sgns::read (stream_a, version_max);
		sgns::read (stream_a, version_using);
		sgns::read (stream_a, version_min_m);
		sgns::read (stream_a, type);
		sgns::read (stream_a, extensions_l);
		extensions = extensions_l;
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

uint8_t sgns::message_header::version_min () const
{
	debug_assert (version_min_m != std::numeric_limits<uint8_t>::max ());
	return version_min_m;
}

sgns::message::message (sgns::message_type type_a) :
header (type_a)
{
}

sgns::message::message (sgns::message_header const & header_a) :
header (header_a)
{
}

std::shared_ptr<std::vector<uint8_t>> sgns::message::to_bytes (bool use_epoch_2_min_version_a) const
{
	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	sgns::vectorstream stream (*bytes);
	serialize (stream, use_epoch_2_min_version_a);
	return bytes;
}

sgns::shared_const_buffer sgns::message::to_shared_const_buffer (bool use_epoch_2_min_version_a) const
{
	return shared_const_buffer (to_bytes (use_epoch_2_min_version_a));
}

sgns::block_type sgns::message_header::block_type () const
{
	return static_cast<sgns::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void sgns::message_header::block_type_set (sgns::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

uint8_t sgns::message_header::count_get () const
{
	return static_cast<uint8_t> (((extensions & count_mask) >> 12).to_ullong ());
}

void sgns::message_header::count_set (uint8_t count_a)
{
	debug_assert (count_a < 16);
	extensions &= ~count_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (count_a) << 12);
}

void sgns::message_header::flag_set (uint8_t flag_a)
{
	// Flags from 8 are block_type & count
	debug_assert (flag_a < 8);
	extensions.set (flag_a, true);
}

bool sgns::message_header::bulk_pull_is_count_present () const
{
	auto result (false);
	if (type == sgns::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_count_present_flag))
		{
			result = true;
		}
	}
	return result;
}

bool sgns::message_header::node_id_handshake_is_query () const
{
	auto result (false);
	if (type == sgns::message_type::node_id_handshake)
	{
		if (extensions.test (node_id_handshake_query_flag))
		{
			result = true;
		}
	}
	return result;
}

bool sgns::message_header::node_id_handshake_is_response () const
{
	auto result (false);
	if (type == sgns::message_type::node_id_handshake)
	{
		if (extensions.test (node_id_handshake_response_flag))
		{
			result = true;
		}
	}
	return result;
}

size_t sgns::message_header::payload_length_bytes () const
{
	switch (type)
	{
		case sgns::message_type::bulk_pull:
		{
			return sgns::bulk_pull::size + (bulk_pull_is_count_present () ? sgns::bulk_pull::extended_parameters_size : 0);
		}
		case sgns::message_type::bulk_push:
		case sgns::message_type::telemetry_req:
		{
			// These don't have a payload
			return 0;
		}
		case sgns::message_type::frontier_req:
		{
			return sgns::frontier_req::size;
		}
		case sgns::message_type::bulk_pull_account:
		{
			return sgns::bulk_pull_account::size;
		}
		case sgns::message_type::keepalive:
		{
			return sgns::keepalive::size;
		}
		case sgns::message_type::publish:
		{
			return sgns::block::size (block_type ());
		}
		case sgns::message_type::confirm_ack:
		{
			return sgns::confirm_ack::size (block_type (), count_get ());
		}
		case sgns::message_type::confirm_req:
		{
			return sgns::confirm_req::size (block_type (), count_get ());
		}
		case sgns::message_type::node_id_handshake:
		{
			return sgns::node_id_handshake::size (*this);
		}
		case sgns::message_type::telemetry_ack:
		{
			return sgns::telemetry_ack::size (*this);
		}
		default:
		{
			debug_assert (false);
			return 0;
		}
	}
}

// MTU - IP header - UDP header
const size_t sgns::message_parser::max_safe_udp_message_size = 508;

std::string sgns::message_parser::status_string ()
{
	switch (status)
	{
		case sgns::message_parser::parse_status::success:
		{
			return "success";
		}
		case sgns::message_parser::parse_status::insufficient_work:
		{
			return "insufficient_work";
		}
		case sgns::message_parser::parse_status::invalid_header:
		{
			return "invalid_header";
		}
		case sgns::message_parser::parse_status::invalid_message_type:
		{
			return "invalid_message_type";
		}
		case sgns::message_parser::parse_status::invalid_keepalive_message:
		{
			return "invalid_keepalive_message";
		}
		case sgns::message_parser::parse_status::invalid_publish_message:
		{
			return "invalid_publish_message";
		}
		case sgns::message_parser::parse_status::invalid_confirm_req_message:
		{
			return "invalid_confirm_req_message";
		}
		case sgns::message_parser::parse_status::invalid_confirm_ack_message:
		{
			return "invalid_confirm_ack_message";
		}
		case sgns::message_parser::parse_status::invalid_node_id_handshake_message:
		{
			return "invalid_node_id_handshake_message";
		}
		case sgns::message_parser::parse_status::invalid_telemetry_req_message:
		{
			return "invalid_telemetry_req_message";
		}
		case sgns::message_parser::parse_status::invalid_telemetry_ack_message:
		{
			return "invalid_telemetry_ack_message";
		}
		case sgns::message_parser::parse_status::outdated_version:
		{
			return "outdated_version";
		}
		case sgns::message_parser::parse_status::invalid_magic:
		{
			return "invalid_magic";
		}
		case sgns::message_parser::parse_status::invalid_network:
		{
			return "invalid_network";
		}
		case sgns::message_parser::parse_status::duplicate_publish_message:
		{
			return "duplicate_publish_message";
		}
	}

	debug_assert (false);

	return "[unknown parse_status]";
}

sgns::message_parser::message_parser (sgns::network_filter & publish_filter_a, sgns::block_uniquer & block_uniquer_a, sgns::vote_uniquer & vote_uniquer_a, sgns::message_visitor & visitor_a, sgns::work_pool & pool_a, bool use_epoch_2_min_version_a) :
publish_filter (publish_filter_a),
block_uniquer (block_uniquer_a),
vote_uniquer (vote_uniquer_a),
visitor (visitor_a),
pool (pool_a),
status (parse_status::success),
use_epoch_2_min_version (use_epoch_2_min_version_a)
{
}

void sgns::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
	static sgns::network_constants network_constants;
	status = parse_status::success;
	auto error (false);
	if (size_a <= max_safe_udp_message_size)
	{
		// Guaranteed to be deliverable
		sgns::bufferstream stream (buffer_a, size_a);
		sgns::message_header header (error, stream);
		if (!error)
		{
			if (header.version_using < get_protocol_constants ().protocol_version_min (use_epoch_2_min_version))
			{
				status = parse_status::outdated_version;
			}
			else
			{
				switch (header.type)
				{
					case sgns::message_type::keepalive:
					{
						deserialize_keepalive (stream, header);
						break;
					}
					case sgns::message_type::publish:
					{
						sgns::uint128_t digest;
						if (!publish_filter.apply (buffer_a + header.size, size_a - header.size, &digest))
						{
							deserialize_publish (stream, header, digest);
						}
						else
						{
							status = parse_status::duplicate_publish_message;
						}
						break;
					}
					case sgns::message_type::confirm_req:
					{
						deserialize_confirm_req (stream, header);
						break;
					}
					case sgns::message_type::confirm_ack:
					{
						deserialize_confirm_ack (stream, header);
						break;
					}
					case sgns::message_type::node_id_handshake:
					{
						deserialize_node_id_handshake (stream, header);
						break;
					}
					case sgns::message_type::telemetry_req:
					{
						deserialize_telemetry_req (stream, header);
						break;
					}
					case sgns::message_type::telemetry_ack:
					{
						deserialize_telemetry_ack (stream, header);
						break;
					}
					default:
					{
						status = parse_status::invalid_message_type;
						break;
					}
				}
			}
		}
		else
		{
			status = parse_status::invalid_header;
		}
	}
}

void sgns::message_parser::deserialize_keepalive (sgns::stream & stream_a, sgns::message_header const & header_a)
{
	auto error (false);
	sgns::keepalive incoming (error, stream_a, header_a);
	if (!error && at_end (stream_a))
	{
		visitor.keepalive (incoming);
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
}

void sgns::message_parser::deserialize_publish (sgns::stream & stream_a, sgns::message_header const & header_a, sgns::uint128_t const & digest_a)
{
	auto error (false);
	sgns::publish incoming (error, stream_a, header_a, digest_a, &block_uniquer);
	if (!error && at_end (stream_a))
	{
		if (!sgns::work_validate_entry (*incoming.block))
		{
			visitor.publish (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
}

void sgns::message_parser::deserialize_confirm_req (sgns::stream & stream_a, sgns::message_header const & header_a)
{
	auto error (false);
	sgns::confirm_req incoming (error, stream_a, header_a, &block_uniquer);
	if (!error && at_end (stream_a))
	{
		if (incoming.block == nullptr || !sgns::work_validate_entry (*incoming.block))
		{
			visitor.confirm_req (incoming);
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
}

void sgns::message_parser::deserialize_confirm_ack (sgns::stream & stream_a, sgns::message_header const & header_a)
{
	auto error (false);
	sgns::confirm_ack incoming (error, stream_a, header_a, &vote_uniquer);
	if (!error && at_end (stream_a))
	{
		for (auto & vote_block : incoming.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<sgns::block>> (vote_block));
				if (sgns::work_validate_entry (*block))
				{
					status = parse_status::insufficient_work;
					break;
				}
			}
		}
		if (status == parse_status::success)
		{
			visitor.confirm_ack (incoming);
		}
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
}

void sgns::message_parser::deserialize_node_id_handshake (sgns::stream & stream_a, sgns::message_header const & header_a)
{
	bool error_l (false);
	sgns::node_id_handshake incoming (error_l, stream_a, header_a);
	if (!error_l && at_end (stream_a))
	{
		visitor.node_id_handshake (incoming);
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
}

void sgns::message_parser::deserialize_telemetry_req (sgns::stream & stream_a, sgns::message_header const & header_a)
{
	sgns::telemetry_req incoming (header_a);
	if (at_end (stream_a))
	{
		visitor.telemetry_req (incoming);
	}
	else
	{
		status = parse_status::invalid_telemetry_req_message;
	}
}

void sgns::message_parser::deserialize_telemetry_ack (sgns::stream & stream_a, sgns::message_header const & header_a)
{
	bool error_l (false);
	sgns::telemetry_ack incoming (error_l, stream_a, header_a);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error_l)
	{
		visitor.telemetry_ack (incoming);
	}
	else
	{
		status = parse_status::invalid_telemetry_ack_message;
	}
}

bool sgns::message_parser::at_end (sgns::stream & stream_a)
{
	uint8_t junk;
	auto end (sgns::try_read (stream_a, junk));
	return end;
}

sgns::keepalive::keepalive () :
message (sgns::message_type::keepalive)
{
	sgns::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

sgns::keepalive::keepalive (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void sgns::keepalive::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void sgns::keepalive::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		debug_assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool sgns::keepalive::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!try_read (stream_a, address) && !try_read (stream_a, port))
		{
			*i = sgns::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool sgns::keepalive::operator== (sgns::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

sgns::publish::publish (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a, sgns::uint128_t const & digest_a, sgns::block_uniquer * uniquer_a) :
message (header_a),
digest (digest_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

sgns::publish::publish (std::shared_ptr<sgns::block> block_a) :
message (sgns::message_type::publish),
block (block_a)
{
	header.block_type_set (block->type ());
}

void sgns::publish::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	debug_assert (block != nullptr);
	header.serialize (stream_a, use_epoch_2_min_version_a);
	block->serialize (stream_a);
}

bool sgns::publish::deserialize (sgns::stream & stream_a, sgns::block_uniquer * uniquer_a)
{
	debug_assert (header.type == sgns::message_type::publish);
	block = sgns::deserialize_block (stream_a, header.block_type (), uniquer_a);
	auto result (block == nullptr);
	return result;
}

void sgns::publish::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool sgns::publish::operator== (sgns::publish const & other_a) const
{
	return *block == *other_a.block;
}

sgns::confirm_req::confirm_req (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a, sgns::block_uniquer * uniquer_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

sgns::confirm_req::confirm_req (std::shared_ptr<sgns::block> block_a) :
message (sgns::message_type::confirm_req),
block (block_a)
{
	header.block_type_set (block->type ());
}

sgns::confirm_req::confirm_req (std::vector<std::pair<sgns::block_hash, sgns::root>> const & roots_hashes_a) :
message (sgns::message_type::confirm_req),
roots_hashes (roots_hashes_a)
{
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (sgns::block_type::not_a_block);
	debug_assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

sgns::confirm_req::confirm_req (sgns::block_hash const & hash_a, sgns::root const & root_a) :
message (sgns::message_type::confirm_req),
roots_hashes (std::vector<std::pair<sgns::block_hash, sgns::root>> (1, std::make_pair (hash_a, root_a)))
{
	debug_assert (!roots_hashes.empty ());
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (sgns::block_type::not_a_block);
	debug_assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

void sgns::confirm_req::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void sgns::confirm_req::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
	if (header.block_type () == sgns::block_type::not_a_block)
	{
		debug_assert (!roots_hashes.empty ());
		// Write hashes & roots
		for (auto & root_hash : roots_hashes)
		{
			write (stream_a, root_hash.first);
			write (stream_a, root_hash.second);
		}
	}
	else
	{
		debug_assert (block != nullptr);
		block->serialize (stream_a);
	}
}

bool sgns::confirm_req::deserialize (sgns::stream & stream_a, sgns::block_uniquer * uniquer_a)
{
	bool result (false);
	debug_assert (header.type == sgns::message_type::confirm_req);
	try
	{
		if (header.block_type () == sgns::block_type::not_a_block)
		{
			uint8_t count (header.count_get ());
			for (auto i (0); i != count && !result; ++i)
			{
				sgns::block_hash block_hash (0);
				sgns::block_hash root (0);
				read (stream_a, block_hash);
				read (stream_a, root);
				if (!block_hash.is_zero () || !root.is_zero ())
				{
					roots_hashes.emplace_back (block_hash, root);
				}
			}

			result = roots_hashes.empty () || (roots_hashes.size () != count);
		}
		else
		{
			block = sgns::deserialize_block (stream_a, header.block_type (), uniquer_a);
			result = block == nullptr;
		}
	}
	catch (const std::runtime_error &)
	{
		result = true;
	}

	return result;
}

bool sgns::confirm_req::operator== (sgns::confirm_req const & other_a) const
{
	bool equal (false);
	if (block != nullptr && other_a.block != nullptr)
	{
		equal = *block == *other_a.block;
	}
	else if (!roots_hashes.empty () && !other_a.roots_hashes.empty ())
	{
		equal = roots_hashes == other_a.roots_hashes;
	}
	return equal;
}

std::string sgns::confirm_req::roots_string () const
{
	std::string result;
	for (auto & root_hash : roots_hashes)
	{
		result += root_hash.first.to_string ();
		result += ":";
		result += root_hash.second.to_string ();
		result += ", ";
	}
	return result;
}

size_t sgns::confirm_req::size (sgns::block_type type_a, size_t count)
{
	size_t result (0);
	if (type_a != sgns::block_type::invalid && type_a != sgns::block_type::not_a_block)
	{
		result = sgns::block::size (type_a);
	}
	else if (type_a == sgns::block_type::not_a_block)
	{
		result = count * (sizeof (sgns::uint256_union) + sizeof (sgns::block_hash));
	}
	return result;
}

sgns::confirm_ack::confirm_ack (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a, sgns::vote_uniquer * uniquer_a) :
message (header_a),
vote (sgns::make_shared<sgns::vote> (error_a, stream_a, header.block_type ()))
{
	if (!error_a && uniquer_a)
	{
		vote = uniquer_a->unique (vote);
	}
}

sgns::confirm_ack::confirm_ack (std::shared_ptr<sgns::vote> vote_a) :
message (sgns::message_type::confirm_ack),
vote (vote_a)
{
	debug_assert (!vote_a->blocks.empty ());
	auto & first_vote_block (vote_a->blocks[0]);
	if (first_vote_block.which ())
	{
		header.block_type_set (sgns::block_type::not_a_block);
		debug_assert (vote_a->blocks.size () < 16);
		header.count_set (static_cast<uint8_t> (vote_a->blocks.size ()));
	}
	else
	{
		header.block_type_set (boost::get<std::shared_ptr<sgns::block>> (first_vote_block)->type ());
	}
}

void sgns::confirm_ack::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	debug_assert (header.block_type () == sgns::block_type::not_a_block || header.block_type () == sgns::block_type::send || header.block_type () == sgns::block_type::receive || header.block_type () == sgns::block_type::open || header.block_type () == sgns::block_type::change || header.block_type () == sgns::block_type::state);
	header.serialize (stream_a, use_epoch_2_min_version_a);
	vote->serialize (stream_a, header.block_type ());
}

bool sgns::confirm_ack::operator== (sgns::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void sgns::confirm_ack::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

size_t sgns::confirm_ack::size (sgns::block_type type_a, size_t count)
{
	size_t result (sizeof (sgns::account) + sizeof (sgns::signature) + sizeof (uint64_t));
	if (type_a != sgns::block_type::invalid && type_a != sgns::block_type::not_a_block)
	{
		result += sgns::block::size (type_a);
	}
	else if (type_a == sgns::block_type::not_a_block)
	{
		result += count * sizeof (sgns::block_hash);
	}
	return result;
}

sgns::frontier_req::frontier_req () :
message (sgns::message_type::frontier_req)
{
}

sgns::frontier_req::frontier_req (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void sgns::frontier_req::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool sgns::frontier_req::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::frontier_req);
	auto error (false);
	try
	{
		sgns::read (stream_a, start.bytes);
		sgns::read (stream_a, age);
		sgns::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void sgns::frontier_req::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool sgns::frontier_req::operator== (sgns::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

sgns::bulk_pull::bulk_pull () :
message (sgns::message_type::bulk_pull)
{
}

sgns::bulk_pull::bulk_pull (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void sgns::bulk_pull::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

void sgns::bulk_pull::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	/*
	 * Ensure the "count_present" flag is set if there
	 * is a limit specifed.  Additionally, do not allow
	 * the "count_present" flag with a value of 0, since
	 * that is a sentinel which we use to mean "all blocks"
	 * and that is the behavior of not having the flag set
	 * so it is wasteful to do this.
	 */
	debug_assert ((count == 0 && !is_count_present ()) || (count != 0 && is_count_present ()));

	header.serialize (stream_a, use_epoch_2_min_version_a);
	write (stream_a, start);
	write (stream_a, end);

	if (is_count_present ())
	{
		std::array<uint8_t, extended_parameters_size> count_buffer{ { 0 } };
		decltype (count) count_little_endian;
		static_assert (sizeof (count_little_endian) < (count_buffer.size () - 1), "count must fit within buffer");

		count_little_endian = boost::endian::native_to_little (count);
		memcpy (count_buffer.data () + 1, &count_little_endian, sizeof (count_little_endian));

		write (stream_a, count_buffer);
	}
}

bool sgns::bulk_pull::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::bulk_pull);
	auto error (false);
	try
	{
		sgns::read (stream_a, start);
		sgns::read (stream_a, end);

		if (is_count_present ())
		{
			std::array<uint8_t, extended_parameters_size> extended_parameters_buffers;
			static_assert (sizeof (count) < (extended_parameters_buffers.size () - 1), "count must fit within buffer");

			sgns::read (stream_a, extended_parameters_buffers);
			if (extended_parameters_buffers.front () != 0)
			{
				error = true;
			}
			else
			{
				memcpy (&count, extended_parameters_buffers.data () + 1, sizeof (count));
				boost::endian::little_to_native_inplace (count);
			}
		}
		else
		{
			count = 0;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool sgns::bulk_pull::is_count_present () const
{
	return header.extensions.test (count_present_flag);
}

void sgns::bulk_pull::set_count_present (bool value_a)
{
	header.extensions.set (count_present_flag, value_a);
}

sgns::bulk_pull_account::bulk_pull_account () :
message (sgns::message_type::bulk_pull_account)
{
}

sgns::bulk_pull_account::bulk_pull_account (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a) :
message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void sgns::bulk_pull_account::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

void sgns::bulk_pull_account::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

bool sgns::bulk_pull_account::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::bulk_pull_account);
	auto error (false);
	try
	{
		sgns::read (stream_a, account);
		sgns::read (stream_a, minimum_amount);
		sgns::read (stream_a, flags);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

sgns::bulk_push::bulk_push () :
message (sgns::message_type::bulk_push)
{
}

sgns::bulk_push::bulk_push (sgns::message_header const & header_a) :
message (header_a)
{
}

bool sgns::bulk_push::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::bulk_push);
	return false;
}

void sgns::bulk_push::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
}

void sgns::bulk_push::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

sgns::telemetry_req::telemetry_req () :
message (sgns::message_type::telemetry_req)
{
}

sgns::telemetry_req::telemetry_req (sgns::message_header const & header_a) :
message (header_a)
{
}

bool sgns::telemetry_req::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::telemetry_req);
	return false;
}

void sgns::telemetry_req::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
}

void sgns::telemetry_req::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.telemetry_req (*this);
}

sgns::telemetry_ack::telemetry_ack () :
message (sgns::message_type::telemetry_ack)
{
}

sgns::telemetry_ack::telemetry_ack (bool & error_a, sgns::stream & stream_a, sgns::message_header const & message_header) :
message (message_header)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

sgns::telemetry_ack::telemetry_ack (sgns::telemetry_data const & telemetry_data_a) :
message (sgns::message_type::telemetry_ack),
data (telemetry_data_a)
{
	header.extensions = telemetry_data::size;
}

void sgns::telemetry_ack::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
	if (!is_empty_payload ())
	{
		data.serialize (stream_a);
	}
}

bool sgns::telemetry_ack::deserialize (sgns::stream & stream_a)
{
	auto error (false);
	debug_assert (header.type == sgns::message_type::telemetry_ack);
	try
	{
		if (!is_empty_payload ())
		{
			data.deserialize (stream_a, header.extensions.to_ulong ());
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void sgns::telemetry_ack::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.telemetry_ack (*this);
}

uint16_t sgns::telemetry_ack::size () const
{
	return size (header);
}

uint16_t sgns::telemetry_ack::size (sgns::message_header const & message_header_a)
{
	return static_cast<uint16_t> (message_header_a.extensions.to_ulong ());
}

bool sgns::telemetry_ack::is_empty_payload () const
{
	return size () == 0;
}

void sgns::telemetry_data::deserialize (sgns::stream & stream_a, uint16_t payload_length_a)
{
	read (stream_a, signature);
	read (stream_a, node_id);
	read (stream_a, block_count);
	read (stream_a, cemented_count);
	read (stream_a, unchecked_count);
	read (stream_a, account_count);
	read (stream_a, bandwidth_cap);
	read (stream_a, peer_count);
	read (stream_a, protocol_version);
	read (stream_a, uptime);
	read (stream_a, genesis_block.bytes);
	read (stream_a, major_version);
	read (stream_a, minor_version);
	read (stream_a, patch_version);
	read (stream_a, pre_release_version);
	read (stream_a, maker);

	uint64_t timestamp_l;
	read (stream_a, timestamp_l);
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
}

void sgns::telemetry_data::serialize_without_signature (sgns::stream & stream_a, uint16_t /* size_a */) const
{
	write (stream_a, node_id);
	write (stream_a, block_count);
	write (stream_a, cemented_count);
	write (stream_a, unchecked_count);
	write (stream_a, account_count);
	write (stream_a, bandwidth_cap);
	write (stream_a, peer_count);
	write (stream_a, protocol_version);
	write (stream_a, uptime);
	write (stream_a, genesis_block.bytes);
	write (stream_a, major_version);
	write (stream_a, minor_version);
	write (stream_a, patch_version);
	write (stream_a, pre_release_version);
	write (stream_a, maker);
	write (stream_a, std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ());
}

void sgns::telemetry_data::serialize (sgns::stream & stream_a) const
{
	write (stream_a, signature);
	serialize_without_signature (stream_a, size);
}

sgns::error_ sgns::telemetry_data::serialize_json (sgns::jsonconfig & json, bool ignore_identification_metrics_a) const
{
	if (!ignore_identification_metrics_a)
	{
		json.put ("signature", signature.to_string ());
		json.put ("node_id", node_id.to_string ());
	}
	json.put ("block_count", block_count);
	json.put ("cemented_count", cemented_count);
	json.put ("unchecked_count", unchecked_count);
	json.put ("account_count", account_count);
	json.put ("bandwidth_cap", bandwidth_cap);
	json.put ("peer_count", peer_count);
	json.put ("protocol_version", protocol_version);
	json.put ("uptime", uptime);
	json.put ("genesis_block", genesis_block.to_string ());
	json.put ("major_version", major_version);
	json.put ("minor_version", minor_version);
	json.put ("patch_version", patch_version);
	json.put ("pre_release_version", pre_release_version);
	json.put ("maker", maker);
	json.put ("timestamp", std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ());
	return json.get_error ();
}

sgns::error_ sgns::telemetry_data::deserialize_json (sgns::jsonconfig & json, bool ignore_identification_metrics_a)
{
	if (!ignore_identification_metrics_a)
	{
		std::string signature_l;
		json.get ("signature", signature_l);
		if (!json.get_error ())
		{
			if (signature.decode_hex (signature_l))
			{
				json.get_error ().set ("Could not deserialize signature");
			}
		}

		std::string node_id_l;
		json.get ("node_id", node_id_l);
		if (!json.get_error ())
		{
			if (node_id.decode_hex (node_id_l))
			{
				json.get_error ().set ("Could not deserialize node id");
			}
		}
	}

	json.get ("block_count", block_count);
	json.get ("cemented_count", cemented_count);
	json.get ("unchecked_count", unchecked_count);
	json.get ("account_count", account_count);
	json.get ("bandwidth_cap", bandwidth_cap);
	json.get ("peer_count", peer_count);
	json.get ("protocol_version", protocol_version);
	json.get ("uptime", uptime);
	std::string genesis_block_l;
	json.get ("genesis_block", genesis_block_l);
	if (!json.get_error ())
	{
		if (genesis_block.decode_hex (genesis_block_l))
		{
			json.get_error ().set ("Could not deserialize genesis block");
		}
	}
	json.get ("major_version", major_version);
	json.get ("minor_version", minor_version);
	json.get ("patch_version", patch_version);
	json.get ("pre_release_version", pre_release_version);
	json.get ("maker", maker);
	auto timestamp_l = json.get<uint64_t> ("timestamp");
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	return json.get_error ();
}

bool sgns::telemetry_data::operator== (sgns::telemetry_data const & data_a) const
{
	return (signature == data_a.signature && node_id == data_a.node_id && block_count == data_a.block_count && cemented_count == data_a.cemented_count && unchecked_count == data_a.unchecked_count && account_count == data_a.account_count && bandwidth_cap == data_a.bandwidth_cap && uptime == data_a.uptime && peer_count == data_a.peer_count && protocol_version == data_a.protocol_version && genesis_block == data_a.genesis_block && major_version == data_a.major_version && minor_version == data_a.minor_version && patch_version == data_a.patch_version && pre_release_version == data_a.pre_release_version && maker == data_a.maker && timestamp == data_a.timestamp);
}

bool sgns::telemetry_data::operator!= (sgns::telemetry_data const & data_a) const
{
	return !(*this == data_a);
}

void sgns::telemetry_data::sign (sgns::keypair const & node_id_a)
{
	debug_assert (node_id == node_id_a.pub);
	std::vector<uint8_t> bytes;
	{
		sgns::vectorstream stream (bytes);
		serialize_without_signature (stream, size);
	}

	signature = sgns::sign_message (node_id_a.prv, node_id_a.pub, bytes.data (), bytes.size ());
}

bool sgns::telemetry_data::validate_signature (uint16_t size_a) const
{
	std::vector<uint8_t> bytes;
	{
		sgns::vectorstream stream (bytes);
		serialize_without_signature (stream, size_a);
	}

	return sgns::validate_message (node_id, bytes.data (), bytes.size (), signature);
}

sgns::node_id_handshake::node_id_handshake (bool & error_a, sgns::stream & stream_a, sgns::message_header const & header_a) :
message (header_a),
query (boost::none),
response (boost::none)
{
	error_a = deserialize (stream_a);
}

sgns::node_id_handshake::node_id_handshake (boost::optional<sgns::uint256_union> query, boost::optional<std::pair<sgns::account, sgns::signature>> response) :
message (sgns::message_type::node_id_handshake),
query (query),
response (response)
{
	if (query)
	{
		header.flag_set (sgns::message_header::node_id_handshake_query_flag);
	}
	if (response)
	{
		header.flag_set (sgns::message_header::node_id_handshake_response_flag);
	}
}

void sgns::node_id_handshake::serialize (sgns::stream & stream_a, bool use_epoch_2_min_version_a) const
{
	header.serialize (stream_a, use_epoch_2_min_version_a);
	if (query)
	{
		write (stream_a, *query);
	}
	if (response)
	{
		write (stream_a, response->first);
		write (stream_a, response->second);
	}
}

bool sgns::node_id_handshake::deserialize (sgns::stream & stream_a)
{
	debug_assert (header.type == sgns::message_type::node_id_handshake);
	auto error (false);
	try
	{
		if (header.node_id_handshake_is_query ())
		{
			sgns::uint256_union query_hash;
			read (stream_a, query_hash);
			query = query_hash;
		}

		if (header.node_id_handshake_is_response ())
		{
			sgns::account response_account;
			read (stream_a, response_account);
			sgns::signature response_signature;
			read (stream_a, response_signature);
			response = std::make_pair (response_account, response_signature);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool sgns::node_id_handshake::operator== (sgns::node_id_handshake const & other_a) const
{
	auto result (*query == *other_a.query && *response == *other_a.response);
	return result;
}

void sgns::node_id_handshake::visit (sgns::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

size_t sgns::node_id_handshake::size () const
{
	return size (header);
}

size_t sgns::node_id_handshake::size (sgns::message_header const & header_a)
{
	size_t result (0);
	if (header_a.node_id_handshake_is_query ())
	{
		result = sizeof (sgns::uint256_union);
	}
	if (header_a.node_id_handshake_is_response ())
	{
		result += sizeof (sgns::account) + sizeof (sgns::signature);
	}
	return result;
}

sgns::message_visitor::~message_visitor ()
{
}

bool sgns::parse_port (std::string const & string_a, uint16_t & port_a)
{
	bool result = false;
	try
	{
		port_a = boost::lexical_cast<uint16_t> (string_a);
	}
	catch (...)
	{
		result = true;
	}
	return result;
}

// Can handle both ipv4 & ipv6 addresses (with and without square brackets)
bool sgns::parse_address (std::string const & address_text_a, boost::asio::ip::address & address_a)
{
	auto address_text = address_text_a;
	if (!address_text.empty () && address_text.front () == '[' && address_text.back () == ']')
	{
		// Chop the square brackets off as make_address doesn't always like them
		address_text = address_text.substr (1, address_text.size () - 2);
	}

	boost::system::error_code address_ec;
	address_a = boost::asio::ip::make_address (address_text, address_ec);
	return !!address_ec;
}

bool sgns::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
	auto result (false);
	auto port_position (string.rfind (':'));
	if (port_position != std::string::npos && port_position > 0)
	{
		std::string port_string (string.substr (port_position + 1));
		try
		{
			uint16_t port;
			result = parse_port (port_string, port);
			if (!result)
			{
				boost::system::error_code ec;
				auto address (boost::asio::ip::make_address_v6 (string.substr (0, port_position), ec));
				if (!ec)
				{
					address_a = address;
					port_a = port;
				}
				else
				{
					result = true;
				}
			}
			else
			{
				result = true;
			}
		}
		catch (...)
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	return result;
}

bool sgns::parse_endpoint (std::string const & string, sgns::endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = sgns::endpoint (address, port);
	}
	return result;
}

bool sgns::parse_tcp_endpoint (std::string const & string, sgns::tcp_endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = sgns::tcp_endpoint (address, port);
	}
	return result;
}

std::chrono::seconds sgns::telemetry_cache_cutoffs::network_to_time (network_constants const & network_constants)
{
	return std::chrono::seconds{ network_constants.is_live_network () ? live : network_constants.is_beta_network () ? beta : test };
}

// sgns::node_singleton_memory_pool_purge_guard::node_singleton_memory_pool_purge_guard () :
// cleanup_guard ({ sgns::block_memory_pool_purge, sgns::purge_singleton_pool_memory<sgns::vote>, sgns::purge_singleton_pool_memory<sgns::election> })
// {
// }
