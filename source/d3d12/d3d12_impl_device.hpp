/*
 * Copyright (C) 2021 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#pragma once

#include "addon_manager.hpp"
#include "descriptor_heap.hpp"
#include <mutex>
#include <dxgi1_5.h>
#include <unordered_map>

namespace reshade::d3d12
{
	class device_impl : public api::api_object_impl<ID3D12Device *, api::device>
	{
		friend class command_list_impl;
		friend class command_queue_impl;

	public:
		explicit device_impl(ID3D12Device *device);
		~device_impl();

		api::device_api get_api() const final { return api::device_api::d3d12; }

		bool check_capability(api::device_caps capability) const final;
		bool check_format_support(api::format format, api::resource_usage usage) const final;

		bool create_sampler(const api::sampler_desc &desc, api::sampler *out) final;
		void destroy_sampler(api::sampler handle) final;

		bool create_resource(const api::resource_desc &desc, const api::subresource_data *initial_data, api::resource_usage initial_state, api::resource *out) final;
		void destroy_resource(api::resource handle) final;

		bool create_resource_view(api::resource resource, api::resource_usage usage_type, const api::resource_view_desc &desc, api::resource_view *out) final;
		void destroy_resource_view(api::resource_view handle) final;

		bool create_pipeline(const api::pipeline_desc &desc, api::pipeline *out) final;
		bool create_compute_pipeline(const api::pipeline_desc &desc, api::pipeline *out);
		bool create_graphics_pipeline(const api::pipeline_desc &desc, api::pipeline *out);
		void destroy_pipeline(api::pipeline_stage type, api::pipeline handle) final;

		bool create_pipeline_layout(uint32_t count, const api::pipeline_layout_param *params, api::pipeline_layout *out) final;
		void destroy_pipeline_layout(api::pipeline_layout handle) final;

		bool create_descriptor_set_layout(uint32_t count, const api::descriptor_range *bindings, bool push_descriptors, api::descriptor_set_layout *out) final;
		void destroy_descriptor_set_layout(api::descriptor_set_layout handle) final;

		bool create_query_pool(api::query_type type, uint32_t size, api::query_pool *out) final;
		void destroy_query_pool(api::query_pool handle) final;

		bool create_render_pass(const api::render_pass_desc &desc, api::render_pass *out) final;
		void destroy_render_pass(api::render_pass handle) final;

		bool create_framebuffer(const api::framebuffer_desc &desc, api::framebuffer *out) final;
		void destroy_framebuffer(api::framebuffer handle) final;

		bool map_resource(api::resource resource, uint32_t subresource, api::map_access access, void **data, uint32_t *row_pitch, uint32_t *slice_pitch) final;
		void unmap_resource(api::resource resource, uint32_t subresource) final;

		void upload_buffer_region(const void *data, api::resource dst, uint64_t dst_offset, uint64_t size) final;
		void upload_texture_region(const api::subresource_data &data, api::resource dst, uint32_t dst_subresource, const int32_t dst_box[6]) final;

		bool get_query_pool_results(api::query_pool pool, uint32_t first, uint32_t count, void *results, uint32_t stride) final;

		bool allocate_descriptor_sets(uint32_t count, const api::descriptor_set_layout *layouts, api::descriptor_set *out) final;
		void free_descriptor_sets(uint32_t count, const api::descriptor_set *sets) final;
		void update_descriptor_sets(uint32_t count, const api::write_descriptor_set *writes) final;

		void wait_idle() const final;

		void set_resource_name(api::resource resource, const char *name) final;

		void get_pipeline_layout_desc(api::pipeline_layout layout, uint32_t *count, api::pipeline_layout_param *params) const final;
		void get_descriptor_set_layout_desc(api::descriptor_set_layout layout, uint32_t *count, api::descriptor_range *bindings) const final;

		api::resource_desc get_resource_desc(api::resource resource) const final;
		void get_resource_from_view(api::resource_view view, api::resource *out) const final;
		bool get_framebuffer_attachment(api::framebuffer framebuffer, api::attachment_type type, uint32_t index, api::resource_view *out) const final;

#if RESHADE_ADDON
		bool resolve_gpu_address(D3D12_GPU_VIRTUAL_ADDRESS address, api::resource *out_resource, uint64_t *out_offset)
		{
			*out_offset = 0;
			*out_resource = { 0 };

			if (!address)
				return true;

			const std::lock_guard<std::mutex> lock(_mutex);

			for (const auto &buffer_info : _buffer_gpu_addresses)
			{
				if (address < buffer_info.second.StartAddress)
					continue;

				const UINT64 address_offset = address - buffer_info.second.StartAddress;
				if (address_offset < buffer_info.second.SizeInBytes)
				{
					*out_offset = address_offset;
					*out_resource = { reinterpret_cast<uintptr_t>(buffer_info.first) };
					return true;
				}
			}

			return false;
		}
		bool resolve_descriptor_handle(D3D12_CPU_DESCRIPTOR_HANDLE handle, D3D12_DESCRIPTOR_HEAP_TYPE type, api::descriptor_set *out_set, uint32_t *out_offset, bool *shader_visible = nullptr)
		{
			*out_set = { 0 };
			*out_offset = 0;

			for (const auto &heap_info : _descriptor_heaps)
			{
				const D3D12_DESCRIPTOR_HEAP_DESC desc = heap_info.first->GetDesc();

				// 'GetCPUDescriptorHandleForHeapStart' returns a descriptor heap index and all CPU descriptor handles use that as alignment
				if (desc.Type != type || (handle.ptr % _descriptor_handle_size[type]) != heap_info.second.StartAddress)
					continue;

				const UINT64 address_offset = handle.ptr - heap_info.second.StartAddress;
				assert(address_offset < heap_info.second.SizeInBytes);

				*out_set = { heap_info.first->GetGPUDescriptorHandleForHeapStart().ptr };
				*out_offset = static_cast<uint32_t>(address_offset / _descriptor_handle_size[desc.Type]);

				if (shader_visible != nullptr)
					*shader_visible = (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
				return true;
			}

			return false;
		}
#endif

		// Cached device capabilities for quick access
		UINT _descriptor_handle_size[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	private:
		mutable std::mutex _mutex;
		std::vector<command_queue_impl *> _queues;
		std::unordered_map<uint64_t, ID3D12Resource *> _views;
		std::vector<std::pair<ID3D12Resource *, D3D12_GPU_VIRTUAL_ADDRESS_RANGE>> _buffer_gpu_addresses;
		std::vector<std::pair<ID3D12DescriptorHeap *, D3D12_GPU_VIRTUAL_ADDRESS_RANGE>> _descriptor_heaps;

		std::unordered_map<UINT64, std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, UINT>> _descriptor_set_map;

		com_ptr<ID3D12PipelineState> _mipmap_pipeline;
		com_ptr<ID3D12RootSignature> _mipmap_signature;

		descriptor_heap_cpu _view_heaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
		descriptor_heap_gpu<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128, 128> _gpu_sampler_heap;
		descriptor_heap_gpu<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, 2048> _gpu_view_heap;

	protected:
		inline void register_resource_view(ID3D12Resource *resource, D3D12_CPU_DESCRIPTOR_HANDLE handle)
		{
			assert(resource != nullptr);
			const std::lock_guard<std::mutex> lock(_mutex);
			_views.emplace(handle.ptr, resource);
		}
#if RESHADE_ADDON
		inline void register_descriptor_heap(ID3D12DescriptorHeap *heap)
		{
			const D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();

			assert(heap != nullptr);
			const std::lock_guard<std::mutex> lock(_mutex);
			_descriptor_heaps.emplace_back(heap, D3D12_GPU_VIRTUAL_ADDRESS_RANGE { heap->GetCPUDescriptorHandleForHeapStart().ptr, desc.NumDescriptors * _descriptor_handle_size[desc.Type] });
			_descriptor_set_map.emplace(heap->GetGPUDescriptorHandleForHeapStart().ptr, std::make_pair(heap->GetCPUDescriptorHandleForHeapStart(), desc.NumDescriptors));
		}
		void unregister_descriptor_heap(ID3D12DescriptorHeap *heap)
		{
			const std::lock_guard<std::mutex> lock(_mutex);
			for (auto it = _descriptor_heaps.begin(); it != _descriptor_heaps.end(); ++it)
			{
				if (it->first == heap)
				{
					it = _descriptor_heaps.erase(it);
					break;
				}
				++it;
			}
			_descriptor_set_map.erase(heap->GetGPUDescriptorHandleForHeapStart().ptr);
		}
		inline void register_buffer_gpu_address(ID3D12Resource *resource, UINT64 size)
		{
			assert(resource != nullptr);
			const std::lock_guard<std::mutex> lock(_mutex);
			_buffer_gpu_addresses.emplace_back(resource, D3D12_GPU_VIRTUAL_ADDRESS_RANGE { resource->GetGPUVirtualAddress(), size });
		}
#endif
	};
}