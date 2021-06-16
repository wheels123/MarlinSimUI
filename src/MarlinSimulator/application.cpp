#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl.h>

#include "user_interface.h"
#include "application.h"

#include "../HAL.h"
#include <src/MarlinCore.h>
#include <src/pins/pinsDebug.h>

Application::Application() {
  sim.vis.create();

  auto serial1 = user_interface.addElement<SerialMonitor>("Serial Monitor(0)", serial_stream_0);
  user_interface.addElement<SerialMonitor>("Serial Monitor(1)", serial_stream_1);
  user_interface.addElement<SerialMonitor>("Serial Monitor(2)", serial_stream_2);
  user_interface.addElement<SerialMonitor>("Serial Monitor(3)", serial_stream_3);

  //user_interface.addElement<TextureWindow>("Controller Display", sim.display.texture_id, (float)sim.display.width / (float)sim.display.height, [this](UiWindow* window){ this->sim.display.ui_callback(window); });
  user_interface.addElement<StatusWindow>("Status", &clear_color, [this](UiWindow* window){ this->sim.ui_info_callback(window); });
  auto components = user_interface.addElement<UiWindow>("Components", [this](UiWindow* window){ this->sim.testPrinter.ui_widgets(); });
  user_interface.addElement<Viewport>("Viewport", [this](UiWindow* window){ this->sim.vis.ui_viewport_callback(window); });
  //user_interface.addElement<GraphWindow>("graphs", sim.display.texture_id, 128.0 / 64.0, std::bind(&Simulation::ui_callback, &sim, std::placeholders::_1));

  user_interface.addElement<UiWindow>("Simulation", [this](UiWindow* window){
    //Simulation Time
    uint64_t time_source = Kernel::SimulationRuntime::nanos();
    uint64_t hours = (time_source / (Kernel::TimeControl::ONE_BILLION * 60 * 60)) ;
    uint64_t remainder = (time_source % (Kernel::TimeControl::ONE_BILLION * 60 * 60));
    uint64_t mins = (remainder / (Kernel::TimeControl::ONE_BILLION * 60));
    remainder = (remainder % (Kernel::TimeControl::ONE_BILLION * 60));
    uint64_t seconds = remainder / (Kernel::TimeControl::ONE_BILLION);
    remainder = remainder % (Kernel::TimeControl::ONE_BILLION);
    ImGui::Text("%02ld:%02ld:%02ld.%09ld", hours, mins, seconds, remainder); //TODO: work around cross platform format string differences
    // Simulation Control
    auto ui_realtime_scale = Kernel::TimeControl::realtime_scale.load();
    ImGui::PushItemWidth(-1);
    ImGui::SliderFloat("##SimSpeed", &ui_realtime_scale, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::PopItemWidth();
    static float resume_scale = ui_realtime_scale;
    static bool paused = false;
    if (!paused) {
      if (ImGui::Button("Pause")) {
        resume_scale = ui_realtime_scale;
        ui_realtime_scale = 0.0f;
        paused = true;
      }
    } else if (ImGui::Button("Resume")) {
      ui_realtime_scale = resume_scale;
      paused = false;
    }

    ImGui::SameLine();
    if (ImGui::Button("Realtime")) { ui_realtime_scale = 1.0f; paused = false; }
    ImGui::SameLine();
    if (ImGui::Button("Max")) { ui_realtime_scale = 100.0f; paused = false; }
    ImGui::SameLine();
    if (ImGui::Button("Break")) Kernel::execution_break();
    Kernel::TimeControl::realtime_scale.store(ui_realtime_scale);
  });

  user_interface.addElement<UiWindow>("Pin List", [this](UiWindow* window){
    for (auto p : pin_array) {
      bool value = Gpio::get_pin_value(p.pin);
      ImGui::Checkbox((std::string("##") + p.name).c_str(), &value);
      ImGui::SameLine();
      ImGui::Text("[%04d]", Gpio::get_pin_value(p.pin));
      ImGui::SameLine();
      ImGui::Text("%s", p.name);
    }
  });

  user_interface.addElement<UiWindow>("Signal Analyser", [this](UiWindow* window){
    struct ScrollingData {
      int MaxSize;
      int Offset;
      ImVector<ImPlotPoint> Data;
      ScrollingData() {
          MaxSize = 100000;
          Offset  = 0;
          Data.reserve(MaxSize);
      }
      void AddPoint(double x, double y) {
          if (Data.size() < MaxSize)
              Data.push_back(ImPlotPoint(x,y));
          else {
              Data[Offset] = ImPlotPoint(x,y);
              Offset =  (Offset + 1) % MaxSize;
          }
      }
      void Erase() {
          if (Data.size() > 0) {
              Data.shrink(0);
              Offset  = 0;
          }
      }
    };

    static pin_type monitor_pin = X_STEP_PIN;
    static const char* label = "Select Pin";
    static char* active_label = (char *)label;
    if(ImGui::BeginCombo("##Select Pin", active_label)) {
      for (auto p : pin_array) {
        if (ImGui::Selectable(p.name, p.pin == monitor_pin)) {
          monitor_pin = p.pin;
          active_label = (char *)p.name;
        }
        if (p.pin == monitor_pin) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (Gpio::pin_map[monitor_pin].event_log.size()) {
      ScrollingData sdata;

      pin_log_data last{};
      for (auto v : Gpio::pin_map[monitor_pin].event_log) {
        if (last.timestamp) sdata.AddPoint(v.timestamp, last.value);
        sdata.AddPoint(v.timestamp, v.value);
        last = v;
      }
      sdata.AddPoint(Kernel::SimulationRuntime::nanos(),  last.value);

      static float window = 10000000000.0f;
      ImGui::SliderFloat("Window", &window, 10.f, 100000000000.f,"%.0f ns");
      static float offset = 0.0f;
      ImGui::SliderFloat("X offset", &offset, 0.f, 10000000000.f,"%.0f ns");
      // ImPlot::SetNextPlotLimitsX(Kernel::SimulationRuntime::nanos() - window - offset, Kernel::SimulationRuntime::nanos() - offset, ImGuiCond_Always);
      // ImPlot::SetNextPlotLimitsY(0.0f, 1.2f, ImGuiCond_Always);
      // static int rt_axis = ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_LockMin;
      // if (ImPlot::BeginPlot("##Scrolling", "Time (ns)", NULL, ImVec2(-1,150), ImPlotAxisFlags_NoTickLabels | ImPlotFlags_Query, rt_axis, rt_axis)) {
      //   ImPlot::PlotLine("pin", &sdata.Data[0].x, &sdata.Data[0].y, sdata.Data.size(), sdata.Offset, sizeof(ImPlotPoint));
      //   ImPlot::EndPlot();
      // }
    }
  });

  user_interface.post_init = [&](){
    //serial1->select();
    //components->select();
    user_interface.ui_elements["Serial Monitor(0)"]->select();
    user_interface.ui_elements["Components"]->select();
  };

}

Application::~Application() {

}

void Application::update() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);

    switch (event.type) {
      case SDL_DROPFILE: {
        char *dropped_filedir = event.drop.file;
        //todo: stream file to the selected serial port?
        //input_file.open(dropped_filedir);
        SDL_free(dropped_filedir);    // Free dropped_filedir memory
      } break;

      case SDL_WINDOWEVENT:
        if (event.window.event != SDL_WINDOWEVENT_CLOSE || event.window.windowID != SDL_GetWindowID((SDL_Window*)window.getHandle()))
          break;

      case SDL_QUIT: active = false; break;
    }
  }

  sim.update();
  user_interface.show();
}

void Application::render() {
  sim.vis.framebuffer->bind();
  glClearColor(clear_color.x, clear_color.y, clear_color.z, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  sim.vis.update();               // Update and render
  sim.vis.framebuffer->render();  // Render and unbind framebuffer

  user_interface.render();
  window.swap_buffers();
}
