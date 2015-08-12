
#include "kernel.hpp"

#include <frigg/funcptr.hpp>
#include <frigg/async.hpp>
#include <frigg/elf.hpp>

#include "../../hel/include/hel.h"

namespace util = frigg::util;
namespace debug = frigg::debug;
namespace async = frigg::async;

namespace thor {
namespace k_init {

util::Tuple<uintptr_t, size_t> calcSegmentMap(uintptr_t address, size_t length) {
	size_t page_size = 0x1000;

	uintptr_t map_page = address / page_size;
	if(length == 0)
		return util::makeTuple(map_page * page_size, size_t(0));
	
	uintptr_t limit = address + length;
	uintptr_t num_pages = (limit / page_size) - map_page;
	if(limit % page_size != 0)
		num_pages++;
	
	return util::makeTuple(map_page * page_size, num_pages * page_size);
}

HelHandle loadSegment(void *image, uintptr_t address, uintptr_t file_offset,
		size_t mem_length, size_t file_length) {
	ASSERT(mem_length > 0);
	util::Tuple<uintptr_t, size_t> map = calcSegmentMap(address, mem_length);

	HelHandle memory;
	helAllocateMemory(map.get<1>(), &memory);
	
	// map the segment memory as read/write and initialize it
	void *write_ptr;
	helMapMemory(memory, kHelNullHandle, nullptr, map.get<1>(),
			kHelMapReadWrite, &write_ptr);

	memset(write_ptr, 0, map.get<1>());
	memcpy((void *)((uintptr_t)write_ptr + (address - map.get<0>())),
			(void *)((uintptr_t)image + file_offset), file_length);
	
	// TODO: unmap the memory region

	return memory;
}

void mapSegment(HelHandle memory, HelHandle space, uintptr_t address,
		size_t length, uint32_t map_flags) {
	ASSERT(length > 0);
	util::Tuple<uintptr_t, size_t> map = calcSegmentMap(address, length);

	void *actual_ptr;
	helMapMemory(memory, space, (void *)map.get<0>(), map.get<1>(),
			map_flags, &actual_ptr);
	ASSERT(actual_ptr == (void *)map.get<0>());
}

void loadImage(const char *path, HelHandle directory) {
	// open and map the executable image into this address space
	HelHandle image_handle;
	helRdOpen(path, strlen(path), &image_handle);

size_t size;
	void *image_ptr;
	helMemoryInfo(image_handle, &size);
	helMapMemory(image_handle, kHelNullHandle, nullptr, size,
			kHelMapReadOnly, &image_ptr);
	
	// create a new 
	HelHandle space;
	helCreateSpace(&space);
	
	Elf64_Ehdr *ehdr = (Elf64_Ehdr*)image_ptr;
	ASSERT(ehdr->e_ident[0] == 0x7F
			&& ehdr->e_ident[1] == 'E'
			&& ehdr->e_ident[2] == 'L'
			&& ehdr->e_ident[3] == 'F');
	ASSERT(ehdr->e_type == ET_EXEC);
	
	for(int i = 0; i < ehdr->e_phnum; i++) {
		auto phdr = (Elf64_Phdr *)((uintptr_t)image_ptr + ehdr->e_phoff
				+ i * ehdr->e_phentsize);

		if(phdr->p_type == PT_LOAD) {
			uint32_t map_flags = 0;
			if((phdr->p_flags & (PF_R | PF_W | PF_X)) == (PF_R | PF_W)) {
				map_flags |= kHelMapReadWrite;
			}else if((phdr->p_flags & (PF_R | PF_W | PF_X)) == (PF_R | PF_X)) {
				map_flags |= kHelMapReadExecute;
			}else{
				debug::panicLogger.log() << "Illegal combination of segment permissions"
						<< debug::Finish();
			}

			HelHandle memory = loadSegment(image_ptr, phdr->p_vaddr,
					phdr->p_offset, phdr->p_memsz, phdr->p_filesz);
			mapSegment(memory, space, phdr->p_vaddr, phdr->p_memsz, map_flags);
		} //FIXME: handle other phdrs
	}
	
	constexpr size_t stack_size = 0x200000;
	
	HelHandle stack_memory;
	helAllocateMemory(stack_size, &stack_memory);

	void *stack_base;
	helMapMemory(stack_memory, space, nullptr,
			stack_size, kHelMapReadWrite, &stack_base);
	
	HelThreadState state;
	memset(&state, 0, sizeof(HelThreadState));
	state.rip = ehdr->e_entry;
	state.rsp = (uintptr_t)stack_base + stack_size;

	HelHandle thread;
	helCreateThread(space, directory, &state, &thread);
}

HelHandle eventHub;

void main() {
	thorRtDisableInts();

	helCreateEventHub(&eventHub);
	
	HelHandle directory;
	helCreateRd(&directory);

	const char *pipe_name = "k_init";
	HelHandle this_end, other_end;
	helCreateBiDirectionPipe(&this_end, &other_end);
	helRdPublish(directory, pipe_name, strlen(pipe_name), other_end);

	loadImage("ld-server", directory);
	
	struct LoadContext {
		LoadContext() : segmentsReceived(0) { }

		size_t numSegments;
		size_t segmentsReceived;
	};

	auto load_action =	async::seq(
		async::lambda([this_end](LoadContext &context,
				util::FuncPtr<void(HelHandle)> callback) {
			// receive a server handle from ld-server
			helSubmitRecvDescriptor(this_end, eventHub, -1, -1, 0,
					(uintptr_t)callback.getFunction(),
					(uintptr_t)callback.getObject());
		}),
		async::lambda([](LoadContext &context,
				util::FuncPtr<void(HelHandle)> callback, HelHandle connect_handle) {
			// connect to the server
			helSubmitConnect(connect_handle, eventHub, 0,
					(uintptr_t)callback.getFunction(),
					(uintptr_t)callback.getObject());
		}),
		async::lambda([](LoadContext &context,
				util::FuncPtr<void()> callback, HelHandle pipe_handle) {
			
		}),
		async::repeatWhile(
			async::lambda([](LoadContext &context,
					util::FuncPtr<void(bool)> callback) {
				callback(context.segmentsReceived < context.numSegments);
			}),
			async::lambda([](LoadContext &context,
					util::FuncPtr<void()> callback) {
				context.segmentsReceived++;
				callback();
			})
		)
	);
	async::run(*kernelAlloc, load_action, LoadContext(), []() {
		infoLogger->log() << "x" << debug::Finish();
	});
	
	while(true) {
		HelEvent events[16];
		size_t num_items;
	
		thorRtEnableInts();
		thorRtDisableInts();

		helWaitForEvents(eventHub, events, 16, kHelWaitInfinite, &num_items);

		for(size_t i = 0; i < num_items; i++) {
			void *function = (void *)events[i].submitFunction;
			void *object = (void *)events[i].submitObject;
			
			switch(events[i].type) {
			case kHelEventRecvDescriptor: {
				typedef void (*FunctionPtr) (void *, HelHandle);
				((FunctionPtr)(function))(object, events[i].handle);
			} break;
			case kHelEventConnect: {
				typedef void (*FunctionPtr) (void *, HelHandle);
				((FunctionPtr)(function))(object, events[i].handle);
			} break;
			default:
				debug::panicLogger.log() << "Unexpected event type"
						<< debug::Finish();
			}
		}
	}
	
	helExitThisThread();
}

} } // namespace thor::k_init

