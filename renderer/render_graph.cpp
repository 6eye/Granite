#include "render_graph.hpp"
#include "type_to_string.hpp"
#include <algorithm>

using namespace std;

namespace Granite
{
RenderTextureResource &RenderPass::add_attachment_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	attachments_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_color_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	color_inputs.push_back(&res);
	color_scale_inputs.push_back(nullptr);
	return res;
}

RenderTextureResource &RenderPass::add_texture_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	texture_inputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::add_color_output(const std::string &name, const AttachmentInfo &info)
{
	auto &res = graph.get_texture_resource(name);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	color_outputs.push_back(&res);
	return res;
}

RenderTextureResource &RenderPass::set_depth_stencil_output(const std::string &name, const AttachmentInfo &info)
{
	auto &res = graph.get_texture_resource(name);
	res.written_in_pass(index);
	res.set_attachment_info(info);
	depth_stencil_output = &res;
	return res;
}

RenderTextureResource &RenderPass::set_depth_stencil_input(const std::string &name)
{
	auto &res = graph.get_texture_resource(name);
	res.read_in_pass(index);
	depth_stencil_input = &res;
	return res;
}

RenderTextureResource &RenderGraph::get_texture_resource(const std::string &name)
{
	auto itr = resource_to_index.find(name);
	if (itr != end(resource_to_index))
	{
		return static_cast<RenderTextureResource &>(*resources[itr->second]);
	}
	else
	{
		unsigned index = resources.size();
		resources.emplace_back(new RenderTextureResource(index));
		resource_to_index[name] = index;
		return static_cast<RenderTextureResource &>(*resources.back());
	}
}

RenderPass &RenderGraph::add_pass(const std::string &name)
{
	auto itr = pass_to_index.find(name);
	if (itr != end(pass_to_index))
	{
		return *passes[itr->second];
	}
	else
	{
		unsigned index = passes.size();
		passes.emplace_back(new RenderPass(*this, index));
		pass_to_index[name] = index;
		return *passes.back();
	}
}

void RenderGraph::set_backbuffer_source(const std::string &name)
{
	backbuffer_source = name;
}

void RenderGraph::validate_passes()
{
	for (auto &pass_ptr : passes)
	{
		auto &pass = *pass_ptr;
		if (!pass.get_color_inputs().empty() && pass.get_color_inputs().size() != pass.get_color_outputs().size())
			throw logic_error("Size of color inputs must match color outputs.");

		if (!pass.get_color_inputs().empty())
		{
			unsigned num_inputs = pass.get_color_inputs().size();
			for (unsigned i = 0; i < num_inputs; i++)
			{
				if (get_resource_dimensions(*pass.get_color_inputs()[i]) != get_resource_dimensions(*pass.get_color_outputs()[i]))
					pass.make_color_input_scaled(i);
			}
		}

		if (pass.get_depth_stencil_input() && pass.get_depth_stencil_output())
		{
			if (get_resource_dimensions(*pass.get_depth_stencil_input()) != get_resource_dimensions(*pass.get_depth_stencil_output()))
				throw logic_error("Dimension mismatch.");
		}
	}
}

void RenderGraph::build_physical_resources()
{
	unsigned phys_index = 0;

	// Find resources which can alias safely.
	for (auto &pass_index : pass_stack)
	{
		auto &pass = *passes[pass_index];

		for (auto *input : pass.get_attachment_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		for (auto *input : pass.get_texture_inputs())
		{
			if (input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		for (auto *input : pass.get_color_scale_inputs())
		{
			if (input && input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*input));
				input->set_physical_index(phys_index++);
			}
		}

		if (!pass.get_color_inputs().empty())
		{
			unsigned size = pass.get_color_inputs().size();
			for (unsigned i = 0; i < size; i++)
			{
				auto *input = pass.get_color_inputs()[i];
				if (input)
				{
					if (input->get_physical_index() == RenderResource::Unused)
					{
						physical_dimensions.push_back(get_resource_dimensions(*input));
						input->set_physical_index(phys_index++);
					}

					if (pass.get_color_outputs()[i]->get_physical_index() == RenderResource::Unused)
						pass.get_color_outputs()[i]->set_physical_index(input->get_physical_index());
					else if (pass.get_color_outputs()[i]->get_physical_index() != input->get_physical_index())
						throw logic_error("Cannot alias resources. Index already claimed.");
				}
			}
		}

		for (auto *output : pass.get_color_outputs())
		{
			if (output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*output));
				output->set_physical_index(phys_index++);
			}
		}

		auto *ds_output = pass.get_depth_stencil_output();
		auto *ds_input = pass.get_depth_stencil_input();
		if (ds_input)
		{
			if (ds_input->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*ds_input));
				ds_input->set_physical_index(phys_index++);
			}

			if (ds_output)
			{
				if (ds_output->get_physical_index() == RenderResource::Unused)
					ds_output->set_physical_index(ds_input->get_physical_index());
				else if (ds_output->get_physical_index() != ds_input->get_physical_index())
					throw logic_error("Cannot alias resources. Index already claimed.");
			}
		}
		else if (ds_output)
		{
			if (ds_output->get_physical_index() == RenderResource::Unused)
			{
				physical_dimensions.push_back(get_resource_dimensions(*ds_output));
				ds_output->set_physical_index(phys_index++);
			}
		}
	}
}

void RenderGraph::build_transients()
{
	for (auto &resource : resources)
	{
		if (resource->get_type() != RenderResource::Type::Texture)
			continue;

		unsigned physical_pass = ~0u;
		bool transient = true;

		for (auto &pass : resource->get_write_passes())
		{
			unsigned phys = passes[pass]->get_physical_pass_index();
			if (physical_pass != ~0u && phys != physical_pass)
			{
				transient = false;
				break;
			}
			physical_pass = phys;
		}

		for (auto &pass : resource->get_read_passes())
		{
			unsigned phys = passes[pass]->get_physical_pass_index();
			if (physical_pass != ~0u && phys != physical_pass)
			{
				transient = false;
				break;
			}
			physical_pass = phys;
		}

		static_cast<RenderTextureResource *>(resource.get())->set_transient_state(transient);
	}
}

void RenderGraph::build_physical_passes()
{
	physical_passes.clear();
	PhysicalPass physical_pass;

	const auto find_attachment = [](const vector<RenderTextureResource *> &resources, const RenderTextureResource *resource) -> bool {
		auto itr = find(begin(resources), end(resources), resource);
		return itr != end(resources);
	};

	const auto should_merge = [&](const RenderPass &prev, const RenderPass &next) -> bool {
		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_texture_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input))
				return false;
			if (input && prev.get_depth_stencil_output() == input)
				return false;
		}

		// Need non-local dependency, cannot merge.
		for (auto *input : next.get_color_scale_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input))
				return false;
		}

		// Keep color on tile.
		for (auto *input : next.get_color_inputs())
		{
			if (!input)
				continue;
			if (find_attachment(prev.get_color_outputs(), input))
				return true;
		}

		// Keep depth on tile.
		if (next.get_depth_stencil_input() && next.get_depth_stencil_input() == prev.get_depth_stencil_output())
			return true;

		// Keep depth attachment or color on-tile.
		for (auto *input : next.get_attachment_inputs())
		{
			if (find_attachment(prev.get_color_outputs(), input))
				return true;
			if (input && prev.get_depth_stencil_output() == input)
				return true;
		}

		return true;
	};

	for (unsigned index = 0; index < pass_stack.size(); )
	{
		unsigned merge_end = index + 1;
		for (; merge_end < pass_stack.size(); merge_end++)
		{
			bool merge = true;
			for (unsigned merge_start = index; merge_start < merge_end; merge_start++)
			{
				if (!should_merge(*passes[pass_stack[merge_start]], *passes[pass_stack[merge_end]]))
				{
					merge = false;
					break;
				}
			}

			if (!merge)
				break;
		}

		physical_pass.passes.insert(end(physical_pass.passes), begin(pass_stack) + index, begin(pass_stack) + merge_end);
		physical_passes.push_back(move(physical_pass));
		index = merge_end;
	}

	for (auto &physical_pass : physical_passes)
	{
		unsigned index = &physical_pass - physical_passes.data();
		for (auto &pass : physical_pass.passes)
			passes[pass]->set_physical_pass_index(index);
	}
}

void RenderGraph::log()
{
	for (auto &resource : physical_dimensions)
	{
		LOGI("Resource #%u: %u x %u (fmt: %u), transient: %s%s\n", unsigned(&resource - physical_dimensions.data()),
		     resource.width, resource.height, unsigned(resource.format), resource.transient ? "yes" : "no",
		     unsigned(&resource - physical_dimensions.data()) == swapchain_physical_index ? " (swapchain)" : "");
	}

	auto barrier_itr = begin(pass_barriers);

	const auto swap_str = [this](const Barrier &barrier) -> const char * {
		return barrier.resource_index == swapchain_physical_index ?
	           " (swapchain)" : "";
	};

	for (auto &barrier : initial_barriers)
	{
		LOGI("DiscardBarrier: %u%s, layout: %s, access: %s\n",
		     barrier.resource_index,
		     swap_str(barrier),
		     Vulkan::layout_to_string(barrier.layout),
		     Vulkan::access_flags_to_string(barrier.access).c_str());
	}

	for (auto &passes : physical_passes)
	{
		LOGI("Physical pass #%u:\n", unsigned(&passes - physical_passes.data()));

		for (auto &barrier : passes.invalidate)
		{
			LOGI("  Invalidate: %u%s, layout: %s, access: %s\n",
			     barrier.resource_index,
			     swap_str(barrier),
			     Vulkan::layout_to_string(barrier.layout),
			     Vulkan::access_flags_to_string(barrier.access).c_str());
		}

		for (auto &subpass : passes.passes)
		{
			LOGI("    Subpass #%u:\n", unsigned(&subpass - passes.passes.data()));
			auto &pass = *this->passes[subpass];

			auto &barriers = *barrier_itr;
			for (auto &barrier : barriers.invalidate)
			{
				if (!physical_dimensions[barrier.resource_index].transient)
				{
					LOGI("      Invalidate: %u%s, layout: %s, access: %s\n",
					     barrier.resource_index,
					     swap_str(barrier),
					     Vulkan::layout_to_string(barrier.layout),
					     Vulkan::access_flags_to_string(barrier.access).c_str());
				}
			}

			if (pass.get_depth_stencil_output())
				LOGI("        DepthStencil RW: %u\n", pass.get_depth_stencil_output()->get_physical_index());
			else if (pass.get_depth_stencil_input())
				LOGI("        DepthStencil ReadOnly: %u\n", pass.get_depth_stencil_input()->get_physical_index());

			for (auto &output : pass.get_color_outputs())
				LOGI("        ColorAttachment #%u: %u\n", unsigned(&output - pass.get_color_outputs().data()), output->get_physical_index());
			for (auto &input : pass.get_attachment_inputs())
				LOGI("        InputAttachment #%u: %u\n", unsigned(&input - pass.get_attachment_inputs().data()), input->get_physical_index());
			for (auto &input : pass.get_texture_inputs())
				LOGI("        Texture #%u: %u\n", unsigned(&input - pass.get_texture_inputs().data()), input->get_physical_index());

			for (auto &input : pass.get_color_scale_inputs())
			{
				if (input)
				{
					LOGI("        ColorScaleInput #%u: %u\n",
					     unsigned(&input - pass.get_color_scale_inputs().data()),
					     input->get_physical_index());
				}
			}

			for (auto &barrier : barriers.flush)
			{
				if (!physical_dimensions[barrier.resource_index].transient &&
					barrier.resource_index != swapchain_physical_index)
				{
					LOGI("      Flush: %u, layout: %s, access: %s\n",
					     barrier.resource_index, Vulkan::layout_to_string(barrier.layout),
					     Vulkan::access_flags_to_string(barrier.access).c_str());
				}
			}

			++barrier_itr;
		}

		for (auto &barrier : passes.flush)
		{
			LOGI("  Flush: %u%s, layout: %s, access: %s\n",
			     barrier.resource_index,
			     swap_str(barrier),
			     Vulkan::layout_to_string(barrier.layout),
			     Vulkan::access_flags_to_string(barrier.access).c_str());
		}
	}
}

void RenderGraph::bake()
{
	validate_passes();

	auto itr = resource_to_index.find(backbuffer_source);
	if (itr == end(resource_to_index))
		throw logic_error("Backbuffer source does not exist.");

	pushed_passes.clear();
	pushed_passes_tmp.clear();
	pass_stack.clear();
	handled_passes.clear();

	auto &backbuffer_resource = *resources[itr->second];

	if (backbuffer_resource.get_write_passes().empty())
		throw logic_error("No pass exists which writes to resource.");

	for (auto &pass : backbuffer_resource.get_write_passes())
	{
		pass_stack.push_back(pass);
		pushed_passes.push_back(pass);
	}

	const auto depend_passes = [&](const std::unordered_set<unsigned> &passes) {
		if (passes.empty())
			throw logic_error("No pass exists which writes to resource.");

		for (auto &pass : passes)
		{
			pushed_passes_tmp.push_back(pass);
			pass_stack.push_back(pass);
		}
	};

	const auto make_unique_list = [](std::vector<unsigned> &passes) {
		sort(begin(passes), end(passes));
		passes.erase(unique(begin(passes), end(passes)), end(passes));
	};

	unsigned iteration_count = 0;

	while (!pushed_passes.empty())
	{
		pushed_passes_tmp.clear();
		make_unique_list(pushed_passes);

		for (auto &pushed_pass : pushed_passes)
		{
			handled_passes.insert(pushed_pass);

			auto &pass = *passes[pushed_pass];
			if (pass.get_depth_stencil_input())
				depend_passes(pass.get_depth_stencil_input()->get_write_passes());
			for (auto *input : pass.get_attachment_inputs())
				depend_passes(input->get_write_passes());
			for (auto *input : pass.get_color_inputs())
				if (input)
					depend_passes(input->get_write_passes());
			for (auto *input : pass.get_color_scale_inputs())
				if (input)
					depend_passes(input->get_write_passes());
			for (auto *input : pass.get_texture_inputs())
				depend_passes(input->get_write_passes());
		}

		pushed_passes.clear();
		swap(pushed_passes, pushed_passes_tmp);

		if (++iteration_count > passes.size())
			throw logic_error("Cycle detected.");
	}

	reverse(begin(pass_stack), end(pass_stack));
	filter_passes(pass_stack);

	build_physical_passes();
	build_transients();
	build_physical_resources();

	pass_barriers.clear();
	pass_barriers.reserve(pass_stack.size());

	build_barriers();

	// Check if the swapchain needs to be blitted to (in case the geometry does not match the backbuffer).
	swapchain_physical_index = resources[resource_to_index[backbuffer_source]]->get_physical_index();
	physical_dimensions[swapchain_physical_index].transient = false;
	if (physical_dimensions[swapchain_physical_index] != swapchain_dimensions)
		swapchain_physical_index = RenderResource::Unused;
	else
		physical_dimensions[swapchain_physical_index].transient = true;

	build_physical_barriers();
}

ResourceDimensions RenderGraph::get_resource_dimensions(const RenderTextureResource &resource) const
{
	ResourceDimensions dim;
	auto &info = resource.get_attachment_info();
	dim.format = info.format;
	dim.transient = resource.get_transient_state();

	switch (info.size_class)
	{
	case SizeClass::SwapchainRelative:
		dim.width = unsigned(info.size_x * swapchain_dimensions.width);
		dim.height = unsigned(info.size_y * swapchain_dimensions.height);
		break;

	case SizeClass::Absolute:
		dim.width = unsigned(info.size_x);
		dim.height = unsigned(info.size_y);
		break;

	case SizeClass::InputRelative:
	{
		auto itr = resource_to_index.find(info.size_relative_name);
		if (itr == end(resource_to_index))
			throw logic_error("Resource does not exist.");
		auto &input = static_cast<RenderTextureResource &>(*resources[itr->second]);
		auto input_dim = get_resource_dimensions(input);

		dim.width = unsigned(input_dim.width * info.size_x);
		dim.height = unsigned(input_dim.height * info.size_y);
		dim.depth = input_dim.depth;
		dim.layers = input_dim.layers;
		dim.levels = input_dim.levels;
		break;
	}
	}

	if (dim.format == VK_FORMAT_UNDEFINED)
		dim.format = swapchain_dimensions.format;

	return dim;
}

void RenderGraph::build_physical_barriers()
{
	initial_barriers.clear();
	auto barrier_itr = begin(pass_barriers);

	const auto flush_access_to_invalidate = [](VkAccessFlags flags) -> VkAccessFlags {
		if (flags & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
			flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		if (flags & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
			flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		return flags;
	};

	struct ResourceState
	{
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkAccessFlags invalidated_types = 0;
		VkAccessFlags flushed_types = 0;

		// If we need to tack on multiple invalidates after the fact ...
		unsigned last_invalidate_pass = RenderPass::Unused;
		unsigned last_flush_pass = RenderPass::Unused;
	};

	// To handle global state.
	vector<ResourceState> global_resource_state(physical_dimensions.size());

	// To handle state inside a physical pass.
	vector<ResourceState> resource_state;
	resource_state.reserve(physical_dimensions.size());

	for (auto &physical_pass : physical_passes)
	{
		resource_state.clear();
		resource_state.resize(physical_dimensions.size());
		unsigned physical_pass_index = unsigned(&physical_pass - physical_passes.data());

		for (auto &subpass : physical_pass.passes)
		{
			auto &barriers = *barrier_itr;
			auto &invalidates = barriers.invalidate;
			auto &flushes = barriers.flush;

			for (auto &invalidate : invalidates)
			{
				// Transients and swapchain images are handled implicitly.
				if (physical_dimensions[invalidate.resource_index].transient ||
					invalidate.resource_index == swapchain_physical_index)
				{
					continue;
				}

				// Only the first use of a resource in a physical pass needs to be handled externally.
				if (resource_state[invalidate.resource_index].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					resource_state[invalidate.resource_index].invalidated_types |= invalidate.access;
					resource_state[invalidate.resource_index].initial_layout = invalidate.layout;
				}

				// All pending flushes have been invalidated in the appropriate stages already.
				resource_state[invalidate.resource_index].flushed_types = 0;
			}

			for (auto &flush : flushes)
			{
				// Transients are handled implicitly.
				if (physical_dimensions[flush.resource_index].transient ||
				    flush.resource_index == swapchain_physical_index)
				{
					continue;
				}

				// The last use of a resource in a physical pass needs to be handled externally.
				resource_state[flush.resource_index].flushed_types |= flush.access;
				resource_state[flush.resource_index].final_layout = flush.layout;

				// This is the first time we used this resource, so queue up initial barriers which transition from
				// UNDEFINED to flush.layout on the start of the frame.
				if (resource_state[flush.resource_index].initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					resource_state[flush.resource_index].initial_layout = flush.layout;
					initial_barriers.push_back({ flush.resource_index, flush.layout, flush_access_to_invalidate(flush.access) });
				}
			}

			++barrier_itr;
		}

		for (auto &resource : resource_state)
		{
			unsigned index = unsigned(&resource - resource_state.data());

			// Do we need to invalidate this resource before starting the pass?
			if (resource.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
			    resource.initial_layout != global_resource_state[index].initial_layout &&
			    resource.invalidated_types & ~global_resource_state[index].invalidated_types)
			{
				Barrier *last_barrier = nullptr;

				if (global_resource_state[index].last_invalidate_pass != RenderPass::Unused)
				{
					unsigned last_pass = global_resource_state[index].last_invalidate_pass;
					auto itr = find_if(begin(physical_passes[last_pass].invalidate), end(physical_passes[last_pass].invalidate), [index](const Barrier &b) {
						return b.resource_index == index;
					});

					if (itr != end(physical_passes[last_pass].invalidate))
						last_barrier = &*itr;
				}

				// If we just need to tack on more access flags, and no layout change is needed, just modify the old barrier.
				if (last_barrier && last_barrier->layout == resource.initial_layout)
					last_barrier->access |= resource.invalidated_types;
				else
				{
					physical_pass.invalidate.push_back(
						{ index, resource.initial_layout, resource.invalidated_types });
					global_resource_state[index].invalidated_types |= resource.invalidated_types;
					global_resource_state[index].current_layout = resource.initial_layout;
					global_resource_state[index].last_invalidate_pass = physical_pass_index;
					global_resource_state[index].last_flush_pass = RenderPass::Unused;
				}
			}

			// Did the pass write anything in this pass which needs to be flushed?
			global_resource_state[index].flushed_types = 0;
			if (resource.flushed_types)
			{
				physical_pass.flush.push_back({ index, resource.final_layout, resource.flushed_types });
				global_resource_state[index].invalidated_types = 0;
				global_resource_state[index].current_layout = resource.final_layout;
				global_resource_state[index].last_invalidate_pass = RenderPass::Unused;
				global_resource_state[index].last_flush_pass = physical_pass_index;
			}
		}
	}
}

void RenderGraph::build_barriers()
{
	const auto get_access = [&](vector<Barrier> &barriers, unsigned index) -> Barrier & {
		auto itr = find_if(begin(barriers), end(barriers), [index](const Barrier &b) {
			return index == b.resource_index;
		});
		if (itr != end(barriers))
			return *itr;
		else
		{
			barriers.push_back({ index, VK_IMAGE_LAYOUT_UNDEFINED, 0 });
			return barriers.back();
		}
	};

	for (auto &index : pass_stack)
	{
		auto &pass = *passes[index];
		Barriers barriers;

		const auto get_invalidate_access = [&](unsigned index) -> Barrier & {
			return get_access(barriers.invalidate, index);
		};

		const auto get_flush_access = [&](unsigned index) -> Barrier & {
			return get_access(barriers.flush, index);
		};

		for (auto *input : pass.get_texture_inputs())
		{
			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *input : pass.get_attachment_inputs())
		{
			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *input : pass.get_color_inputs())
		{
			if (!input)
				continue;

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		for (auto *input : pass.get_color_scale_inputs())
		{
			if (!input)
				continue;

			auto &barrier = get_invalidate_access(input->get_physical_index());
			barrier.access |= VK_ACCESS_SHADER_READ_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		for (auto *output : pass.get_color_outputs())
		{
			auto &barrier = get_flush_access(output->get_physical_index());
			barrier.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			if (barrier.layout != VK_IMAGE_LAYOUT_UNDEFINED)
				throw logic_error("Layout mismatch.");
			barrier.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		auto *output = pass.get_depth_stencil_output();
		auto *input = pass.get_depth_stencil_input();

		if (output && input)
		{
			auto &dst_barrier = get_invalidate_access(input->get_physical_index());
			auto &src_barrier = get_flush_access(output->get_physical_index());

			if (dst_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				dst_barrier.layout = VK_IMAGE_LAYOUT_GENERAL;
			else
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			dst_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			src_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			src_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		else if (input)
		{
			auto &dst_barrier = get_invalidate_access(input->get_physical_index());
			if (dst_barrier.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			else
				dst_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			dst_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}
		else if (output)
		{
			auto &src_barrier = get_flush_access(output->get_physical_index());
			src_barrier.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			src_barrier.access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		pass_barriers.push_back(move(barriers));
	}
}

void RenderGraph::filter_passes(std::vector<unsigned> &list)
{
	unordered_set<unsigned> seen;

	auto output_itr = begin(list);
	for (auto itr = begin(list); itr != end(list); ++itr)
	{
		if (!seen.count(*itr))
		{
			*output_itr = *itr;
			seen.insert(*itr);
			++output_itr;
		}
	}
	list.erase(output_itr, end(list));
}

void RenderGraph::reset()
{
	passes.clear();
	resources.clear();
	pass_to_index.clear();
	resource_to_index.clear();
	physical_passes.clear();
}

}