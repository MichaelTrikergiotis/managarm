
#include "kernel.hpp"

namespace thor {

AsyncRingItem::AsyncRingItem(AsyncData data,
		DirectSpaceLock<HelRingBuffer> space_lock, size_t buffer_size)
: AsyncOperation(frigg::move(data)),
		spaceLock(frigg::move(space_lock)), bufferSize(buffer_size), offset(0) { }

RingBuffer::RingBuffer() { }

// TODO: protect this with a lock
void RingBuffer::submitBuffer(frigg::SharedPtr<AsyncRingItem> item) {
	_bufferQueue.addBack(frigg::move(item));
}

// TODO: protect this with a lock
void RingBuffer::doTransfer(frigg::SharedPtr<AsyncSendString> send,
		frigg::SharedPtr<AsyncRecvString> recv) {
	assert(!_bufferQueue.empty());

	AsyncRingItem &front = *_bufferQueue.front();

	if(front.offset + send->kernelBuffer.size() <= front.bufferSize) {
		size_t offset = front.offset;
		front.offset += send->kernelBuffer.size();

		__atomic_add_fetch(&front.spaceLock->refCount, 1, __ATOMIC_RELEASE);

		frigg::UnsafePtr<AddressSpace> space = front.spaceLock.space();
		auto address = (char *)front.spaceLock.foreignAddress() + sizeof(HelRingBuffer) + offset;
		auto data_lock = ForeignSpaceLock::acquire(space.toShared(), address,
				send->kernelBuffer.size());
		data_lock.copyTo(send->kernelBuffer.data(), send->kernelBuffer.size());

			assert(!"Fix ring buffer events");
		{ // post the send event
/*			UserEvent event(UserEvent::kTypeSendString, send->submitInfo);
		
			frigg::SharedPtr<EventHub> event_hub(send->eventHub);
			EventHub::Guard hub_guard(&event_hub->lock);
			event_hub->raiseEvent(hub_guard, frigg::move(event));
			hub_guard.unlock();*/
		}
		// post the receive event
		{
/*			UserEvent event(UserEvent::kTypeRecvStringTransferToQueue, recv->submitInfo);
			event.length = send->kernelBuffer.size();
			event.offset = offset;
			event.msgRequest = send->msgRequest;
			event.msgSequence = send->msgSequence;
		
			frigg::SharedPtr<EventHub> event_hub = recv->eventHub.grab();
			assert(event_hub);
			EventHub::Guard hub_guard(&event_hub->lock);
			event_hub->raiseEvent(hub_guard, frigg::move(recv));*/
		}
	}else{
		assert(!"TODO: Return the buffer to user-space");
	}
}

};
