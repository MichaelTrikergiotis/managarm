
#include <helix/await.hpp>

#include "fs.pb.h"
#include "protocols/fs/client.hpp"

using M = helix::AwaitMechanism;

namespace protocols {
namespace fs {

File::File(helix::UniqueDescriptor lane)
: _lane(std::move(lane)) { }

COFIBER_ROUTINE(cofiber::future<void>, File::seekAbsolute(int64_t offset), ([=] {
	helix::Offer<M> offer;
	helix::SendBuffer<M> send_req;
	helix::RecvBuffer<M> recv_resp;

	managarm::fs::CntRequest req;
	req.set_req_type(managarm::fs::CntReqType::SEEK_ABS);
	req.set_rel_offset(offset);

	auto ser = req.SerializeAsString();
	uint8_t buffer[128];
	helix::submitAsync(_lane, {
		helix::action(&offer, kHelItemAncillary),
		helix::action(&send_req, ser.data(), ser.size(), kHelItemChain),
		helix::action(&recv_resp, buffer, 128),
	}, helix::Dispatcher::global());

	COFIBER_AWAIT offer.future();
	COFIBER_AWAIT send_req.future();
	COFIBER_AWAIT recv_resp.future();
	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_resp.error());

	managarm::fs::SvrResponse resp;
	resp.ParseFromArray(buffer, recv_resp.actualLength());
	assert(resp.error() == managarm::fs::Errors::SUCCESS);
	COFIBER_RETURN();
}))

COFIBER_ROUTINE(cofiber::future<size_t>, File::readSome(void *data, size_t max_length), ([=] {
	helix::Offer<M> offer;
	helix::SendBuffer<M> send_req;
	helix::RecvBuffer<M> recv_resp;
	helix::RecvBuffer<M> recv_data;

	managarm::fs::CntRequest req;
	req.set_req_type(managarm::fs::CntReqType::READ);
	req.set_size(max_length);

	auto ser = req.SerializeAsString();
	uint8_t buffer[128];
	helix::submitAsync(_lane, {
		helix::action(&offer, kHelItemAncillary),
		helix::action(&send_req, ser.data(), ser.size(), kHelItemChain),
		helix::action(&recv_resp, buffer, 128, kHelItemChain),
		helix::action(&recv_data, data, max_length)
	}, helix::Dispatcher::global());

	COFIBER_AWAIT offer.future();
	COFIBER_AWAIT send_req.future();
	COFIBER_AWAIT recv_resp.future();
	COFIBER_AWAIT recv_data.future();
	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_resp.error());
	HEL_CHECK(recv_data.error());

	managarm::fs::SvrResponse resp;
	resp.ParseFromArray(buffer, recv_resp.actualLength());
	if(resp.error() == managarm::fs::Errors::END_OF_FILE) {
		COFIBER_RETURN(0);
	}
	assert(resp.error() == managarm::fs::Errors::SUCCESS);
	COFIBER_RETURN(recv_data.actualLength());
}))	

COFIBER_ROUTINE(cofiber::future<helix::UniqueDescriptor>, File::accessMemory(), ([=] {
	helix::Offer<M> offer;
	helix::SendBuffer<M> send_req;
	helix::RecvBuffer<M> recv_resp;
	helix::PullDescriptor<M> recv_memory;

	managarm::fs::CntRequest req;
	req.set_req_type(managarm::fs::CntReqType::MMAP);

	auto ser = req.SerializeAsString();
	uint8_t buffer[128];
	helix::submitAsync(_lane, {
		helix::action(&offer, kHelItemAncillary),
		helix::action(&send_req, ser.data(), ser.size(), kHelItemChain),
		helix::action(&recv_resp, buffer, 128, kHelItemChain),
		helix::action(&recv_memory)
	}, helix::Dispatcher::global());

	COFIBER_AWAIT offer.future();
	COFIBER_AWAIT send_req.future();
	COFIBER_AWAIT recv_resp.future();
	COFIBER_AWAIT recv_memory.future();
	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_resp.error());
	HEL_CHECK(recv_memory.error());

	managarm::fs::SvrResponse resp;
	resp.ParseFromArray(buffer, recv_resp.actualLength());
	assert(resp.error() == managarm::fs::Errors::SUCCESS);
	COFIBER_RETURN(recv_memory.descriptor());
}))

} } // namespace protocol::fs
