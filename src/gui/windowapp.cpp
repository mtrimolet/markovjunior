module;
#include <stormkit/gpu/vulkan.hpp>
#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#undef assert
module gui.windowapp;

import log;
import stormkit.core;

import grid;
import geometry;

import engine.model;
import engine.rulenode;
import parser;
import controls;

import stormkit.wsi;
import stormkit.gpu;
// import gui.render;

namespace stk  = stormkit;
namespace stkg = stk::gpu;
namespace stkw = stk::wsi;
namespace stdr = std::ranges;
// namespace stdv = std::views;

using namespace std::string_literals;
using namespace std::chrono_literals;

using clk = std::chrono::high_resolution_clock;

static const auto DEFAULT_PALETTE_FILE = "resources/palette.xml"s;
static const auto DEFAULT_MODEL_FILE   = "models/GoToGradient.xml"s;

static constexpr auto DEFAULT_GRID_EXTENT = std::dims<3>{1u, 59u, 59u};
static constexpr auto DEFAULT_TICKRATE = 60;

static constexpr auto WINDOW_TITLE = "MarkovJunior";
static constexpr auto WINDOW_SIZE  = stk::math::Extent2<stk::u32>{800, 600};
static constexpr auto BUFFERING_COUNT = 2;

struct SubmissionResource {
    stkg::Fence         in_flight;
    stkg::Semaphore     image_available;
    stkg::CommandBuffer render_cmb;
};

struct SwapchainImageResource {
    stk::Ref<const stkg::Image> image;
    stkg::ImageView        view;
    stkg::FrameBuffer      framebuffer;
    stkg::Semaphore        render_finished;
};

auto WindowApp::operator()(std::span<const std::string_view> args) noexcept -> int {
  stkw::parse_args(args);
  
  auto palettefile = DEFAULT_PALETTE_FILE;
  ilog("loading palette");
  auto default_palette = parser::Palette(parser::document(palettefile));

  auto modelarg = stdr::find_if(args, [](const auto& arg) static noexcept {
    return stdr::cbegin(stdr::search(arg, "models/"s)) == stdr::cbegin(arg);
  });
  auto modelfile =
    modelarg != stdr::end(args) ? std::string{*modelarg} : DEFAULT_MODEL_FILE;

  ilog("loading model");
  auto model = parser::Model(parser::document(modelfile));

  auto extent = DEFAULT_GRID_EXTENT;
  auto grid = TracedGrid{extent, model.symbols[0]};
  if (model.origin) grid[grid.area().center()] = model.symbols[1];

  auto controls = Controls {
    .tickrate = DEFAULT_TICKRATE,
    .onReset = [&grid, &model]{
      reset(model.program);
      grid = TracedGrid{grid.extents, model.symbols[0]};
      if (model.origin) grid[grid.area().center()] = model.symbols[1];
    },
  };

  ilog("start program thread");
  auto program_thread = std::jthread{ [&grid, &model, &controls](std::stop_token stop) mutable noexcept {
    auto last_time = clk::now();
    for (auto _ : model.program.visit([&grid](auto& f) noexcept { return f(grid); })) { // TODO replace with a while loop conditioned on the stop token, 
      if (stop.stop_requested()) break;  //      and put the program tick inside (instead of async generator)

      controls.rate_limit(last_time);
      controls.handle_next();
      controls.wait_unpause(stop);

      last_time = clk::now();
    }

    model.halted = true;
  } };

  ilog("open stormkit window");
  auto window = stkw::Window::open(
    WINDOW_TITLE, WINDOW_SIZE, stkw::WindowFlag::DEFAULT | stkw::WindowFlag::EXTERNAL_CONTEXT
  );

  ilog("init stormkit vulkan");
  *stkg::initialize_backend().transform_error(stk::monadic::assert("Failed to initialize gpu backend"));

  ilog("create gpu instance and attach surface to window");
  const auto instance = stkg::Instance::create(WINDOW_TITLE)
                          .transform_error(stk::monadic::assert("Failed to initialize gpu instance"))
                          .value();
  const auto surface = stkg::Surface::create_from_window(instance, window)
                         .transform_error(stk::monadic::assert("Failed to initialize window gpu surface"))
                         .value();

  // pick the best physical device
  const auto& physical_devices = instance.physical_devices();
  if (stdr::empty(physical_devices)) {
      elog("No render physical device found!");
      return 0;
  }
  ilog("Physical devices: {}", physical_devices);

  auto physical_device = stk::as_ref(physical_devices.front());
  auto score           = stkg::score_physical_device(physical_device);
  for (auto i = 1u; i < stdr::size(physical_devices); ++i) {
      const auto& d       = physical_devices[i];
      const auto  d_score = stkg::score_physical_device(d);
      if (d_score > score) {
          physical_device = stk::as_ref(d);
          score           = d_score;
      }
  }
  // auto _physical_device = stdr::max(
  //   physical_devices,
  //   stdr::greater {},
  //   stkg::score_physical_device
  // );

  ilog("Picked gpu: {}", *physical_device);

  // create gpu device
  const auto device = stkg::Device::create(*physical_device, instance)
                        .transform_error(stk::monadic::assert("Failed to initialize gpu device"))
                        .value();

  // create swapchain
  const auto window_extent = window.extent();
  const auto swapchain     = stkg::SwapChain::create(device, surface, window_extent)
                           .transform_error(stk::monadic::assert("Failed to create swapchain"))
                           .value();

  const auto raster_queue = stkg::Queue::create(device, device.raster_queue_entry());

  const auto command_pool
    = stkg::CommandPool::create(device)
        .transform_error(stk::monadic::assert("Failed to create raster queue command pool"))
        .value();

  // imgui related features
  
  static constexpr auto POOL_SIZES = std::array {
      stkg::DescriptorPool::Size { .type            = stkg::DescriptorType::COMBINED_IMAGE_SAMPLER,
                                 .descriptor_count = BUFFERING_COUNT /* IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE */ }
  };

  const auto descriptor_pool
    = stkg::DescriptorPool::create(device, POOL_SIZES, BUFFERING_COUNT)
         .transform_error(stk::monadic::assert("Failed to create descriptor pool"))
         .value();

  // const auto path          = std::filesystem::path { u8"build/shaders/triangle.spv" };
  // const auto vertex_shader = stkg::Shader::load_from_file(device,
  //                                                        // SHADER_DIR "/triangle.spv",
  //                                                        path,
  //                                                        stkg::ShaderStageFlag::VERTEX)
  //                              .transform_error(stk::monadic::assert("Failed to load vertex shader"))
  //                              .value();

  // const auto fragment_shader = stkg::Shader::load_from_file(device,
  //                                                          SHADER_DIR "/triangle.spv",
  //                                                          stkg::ShaderStageFlag::FRAGMENT)
  //                                .transform_error(stk::monadic::assert("Failed to load fragment shader"))
  //                                .value();

  // const auto pipeline_layout = stkg::PipelineLayout::create(device, {})
  //                                .transform_error(stk::monadic::assert("Failed to create pipeline layout"))
  //                                .value();

  // initialize render pass
  const auto render_pass
    = stkg::RenderPass::
        create(device,
               { .attachments = { { .format = swapchain.pixel_format() } },
                 .subpasses   = { { .bind_point            = stkg::PipelineBindPoint::GRAPHICS,
                                    .color_attachment_refs = { { .attachment_id = 0u } } } } })
          .transform_error(stk::monadic::assert("Failed to create render pass"))
          .value();

  // initialize render pipeline
  // const auto window_viewport = stkg::Viewport {
  //     .position = { 0.f, 0.f },
  //     .extent   = window_extent.to<f32>(),
  //     .depth    = { 0.f, 1.f },
  // };
  // const auto scissor = stkg::Scissor {
  //     .offset = { 0, 0 },
  //     .extent = window_extent,
  // };

  // const auto state = stkg::RasterPipelineState {
  //     .input_assembly_state = { .topology = stkg::PrimitiveTopology::TRIANGLE_LIST, },
  //     .viewport_state       = { .viewports = { window_viewport },
  //                              .scissors  = { scissor }, },
  //     .color_blend_state
  //     = { .attachments = { { .blend_enable           = true,
  //                            .src_color_blend_factor = stkg::BlendFactor::SRC_ALPHA,
  //                            .dst_color_blend_factor = stkg::BlendFactor::ONE_MINUS_SRC_ALPHA,
  //                            .src_alpha_blend_factor = stkg::BlendFactor::SRC_ALPHA,
  //                            .dst_alpha_blend_factor = stkg::BlendFactor::ONE_MINUS_SRC_ALPHA,
  //                            .alpha_blend_operation  = stkg::BlendOperation::ADD, }, }, },
  //     .shader_state  = stk::to_refs(vertex_shader, fragment_shader),
  // };

  // const auto pipeline = stkg::Pipeline::create(device, state, pipeline_layout, render_pass)
  //                         .transform_error(stk::monadic::assert("Failed to create raster pipeline"))
  //                         .value();

  // create present engine resources
  auto submission_resources = stk::init_by<std::vector<SubmissionResource>>([&](auto& out) noexcept {
      out.reserve(BUFFERING_COUNT);
      for (auto _ : stk::range(BUFFERING_COUNT)) {
          out.push_back({
            .in_flight = stkg::Fence::create_signaled(device)
                           .transform_error(stk::monadic::assert("Failed to create swapchain image in flight fence"))
                           .value(),
            .image_available = stkg::Semaphore::create(device)
                                 .transform_error(stk::monadic::assert("Failed to create present wait semaphore"))
                                 .value(),
            .render_cmb = command_pool.create_command_buffer()
                            .transform_error(stk::monadic::assert("Failed to create transition command buffers"))
                            .value(),
          });
      }
  });

  // transition swapchain image to present image
  const auto& images = swapchain.images();
  const auto image_count = stdr::size(images);

  auto transition_cmbs
    = command_pool.create_command_buffers(image_count)
        .transform_error(stk::monadic::assert("Failed to create transition command buffers"))
        .value();

  auto image_resources = std::vector<SwapchainImageResource> {};
  image_resources.reserve(stdr::size(images));

  auto image_index = 0u;
  for (const auto& swap_image : images) {
      auto view = stkg::ImageView::create(device, swap_image)
                    .transform_error(stk::monadic::
                                       assert("Failed to create swapchain image view"))
                    .value();
      auto framebuffer = render_pass.create_frame_buffer(device, window_extent, stk::to_refs(view))
                           .transform_error(stk::monadic::assert(std::format("Failed to create framebuffer for image {}", image_index)))
                           .value();

      image_resources.push_back({
        .image           = stk::as_ref(swap_image),
        .view            = std::move(view),
        .framebuffer     = std::move(framebuffer),
        .render_finished = stkg::Semaphore::create(device)
                             .transform_error(stk::monadic::assert("Failed to create render signal semaphore"))
                             .value(),
      });

      auto& transition_cmb = transition_cmbs[image_index];
      *transition_cmb.begin(true)
         .transform_error(stk::monadic::assert("Failed to begin texture transition command buffer"))
         .value()
         ->begin_debug_region(std::format("transition image {}", image_index))
         .transition_image_layout(swap_image,
                                  stkg::ImageLayout::UNDEFINED,
                                  stkg::ImageLayout::PRESENT_SRC)
         .end_debug_region()
         .end()
         .transform_error(stk::monadic::assert("Failed to begin texture transition command buffer"));

      ++image_index;
  }

  const auto fence = stkg::Fence::create(device)
                       .transform_error(stk::monadic::assert("Failed to create transition fence"))
                       .value();

  const auto cmbs = stk::to_refs(transition_cmbs);

  raster_queue.submit({ .command_buffers = cmbs }, stk::as_ref(fence))
    .transform_error(stk::monadic::assert("Failed to submit texture transition command buffers"))
    .value();

  // wait for transition to be done
  fence.wait().transform_error(stk::monadic::assert());

  ilog("loading imgui");
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize.x = window_extent.to<stk::f32>().width;
  io.DisplaySize.y = window_extent.to<stk::f32>().height;

  ilog("init vulkan imgui");
  auto init_info = ImGui_ImplVulkan_InitInfo {
    .ApiVersion = VK_API_VERSION_1_1,
    .Instance = instance.native_handle(),
    .PhysicalDevice = physical_device->native_handle(),
    .Device = device.native_handle(),
    .QueueFamily = 0,
    .Queue = raster_queue.native_handle(),
    .DescriptorPool = descriptor_pool.native_handle(),
    .DescriptorPoolSize = 0,
    .MinImageCount = BUFFERING_COUNT,
    .ImageCount = BUFFERING_COUNT,
    .PipelineCache = nullptr,
    .PipelineInfoMain = {
      .RenderPass = render_pass.native_handle(),
      .Subpass = 0,
      .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
      .PipelineRenderingCreateInfo = {},
    },
    .UseDynamicRendering = false,
    .Allocator = nullptr,
    .CheckVkResultFn = [](VkResult err) static noexcept {
      stk::expects(err == 0, std::format("[vulkan] Error: VkResult = {}", err));
    },
    .MinAllocationSize = 1024 * 1024,
    .CustomShaderVertCreateInfo = {},
    .CustomShaderFragCreateInfo = {},
  };
  ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, stkg::imgui_vk_loader, std::bit_cast<void*>(&device));
  ImGui_ImplVulkan_Init(&init_info);

  window.on(
    stkw::KeyDownEventFunc{ [&window, &io](stk::u8 /*id*/, stkw::Key key, char c) mutable noexcept {
      if (key == stkw::Key::ESCAPE) window.close();
      io.AddInputCharactersUTF8(&c);
    } },
    stkw::MouseMovedEventFunc{ [&io](stk::u8 /*id*/, const stk::math::vec2i& position) mutable noexcept {
      const auto _position = position.to<stk::f32>();
      io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
      io.AddMousePosEvent(_position.x, _position.y);
    } },
    stkw::MouseButtonDownEventFunc{ [&io](stk::u8 /*id*/, stkw::MouseButton button, const stk::math::vec2i&) mutable noexcept {
      auto mouse_button = -1;
      switch (button) {
        case stkw::MouseButton::LEFT:     mouse_button = 0; break;
        case stkw::MouseButton::RIGHT:    mouse_button = 1; break;
        case stkw::MouseButton::MIDDLE:   mouse_button = 2; break;
        case stkw::MouseButton::BUTTON_1: mouse_button = 3; break;
        case stkw::MouseButton::BUTTON_2: mouse_button = 4; break;
        default: return;
      }
      io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
      io.AddMouseButtonEvent(mouse_button, true);
    } },
    stkw::MouseButtonUpEventFunc{ [&io](stk::u8 /*id*/, stkw::MouseButton button, const stk::math::vec2i&) mutable noexcept {
      auto mouse_button = -1;
      switch (button) {
        case stkw::MouseButton::LEFT:     mouse_button = 0; break;
        case stkw::MouseButton::RIGHT:    mouse_button = 1; break;
        case stkw::MouseButton::MIDDLE:   mouse_button = 2; break;
        case stkw::MouseButton::BUTTON_1: mouse_button = 3; break;
        case stkw::MouseButton::BUTTON_2: mouse_button = 4; break;
        default: return;
      }
      io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
      io.AddMouseButtonEvent(mouse_button, false);
    } }
  );

  auto current_frame = 0uz;
  window.event_loop([&] mutable noexcept {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    // TODO replace with copy of grid, potentials, etc...
    ImGui::ShowMetricsWindow(); // Show demo window! :)
    // gui::MainView(grid, model, controls, palette);
    // gui::MainView(grid, model, controls);
    
    ImGui::Render();

    // get next swapchain image
    auto& submission_resource = submission_resources[current_frame];

    const auto& wait      = submission_resource.image_available;
    auto&       in_flight = submission_resource.in_flight;

    const auto acquire_next_image = bind_front(&stkg::SwapChain::acquire_next_image,
                                               &swapchain,
                                               100ms,
                                               std::cref(wait));
    const auto extract_index      = [](auto&& _result) static noexcept {
        auto&& [result, _image_index] = _result;
        return _image_index;
    };

    const auto image_index
      = in_flight.wait()
          .transform([&in_flight](auto&&) mutable noexcept { in_flight.reset(); })
          .and_then(acquire_next_image)
          .transform(extract_index)
          .transform_error(stk::monadic::assert("Failed to acquire next swapchain image"))
          .value();

    const auto& swapchain_image_resource = image_resources[image_index];
    const auto& framebuffer              = swapchain_image_resource.framebuffer;
    const auto& signal                   = swapchain_image_resource.render_finished;

    static constexpr auto PIPELINE_FLAGS = std::array {
        stkg::PipelineStageFlag::COLOR_ATTACHMENT_OUTPUT
    };

    // render in it
    auto& render_cmb = submission_resource.render_cmb;
    render_cmb.reset()
       .transform_error(stk::monadic::assert("Failed to reset render command buffer"))
       .value()
       ->begin()
       .transform_error(stk::monadic::assert("Failed to begin render command buffer"))
       .value()
       ->begin_debug_region("Render imgui")
       .begin_render_pass(render_pass, framebuffer);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), render_cmb.native_handle());

    *render_cmb.end_render_pass()
       .end()
       .transform_error(stk::monadic::assert("Failed to end render command buffer"))
       .value()
       ->submit(raster_queue, stk::as_refs(wait), PIPELINE_FLAGS, stk::as_refs(signal), stk::as_ref(in_flight))
       .transform_error(stk::monadic::assert("Failed to submit render command buffer"));

    // present it
    auto update_current_frame = [&current_frame](auto&&) mutable noexcept {
        if (++current_frame >= BUFFERING_COUNT) current_frame = 0;
    };

    raster_queue.present(stk::as_refs(swapchain), stk::as_refs(signal), stk::as_view(image_index))
      .transform(update_current_frame)
      .transform_error(stk::monadic::assert("Failed to present swapchain image"));

    // std::this_thread::yield();
  });
  
  raster_queue.wait_idle();
  device.wait_idle();

  ImGui_ImplVulkan_Shutdown();
  ImGui::DestroyContext();

  return 0;
}
