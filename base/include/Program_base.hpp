#pragma once
#include <vulkan/vulkan.hpp>
#include "assert.hpp"
#include "Timer.hpp"
#include "FPS_counter.hpp"
#include "Physical_device.hpp"
#include "Device.hpp"
#include <iostream>
#define DEBUG_REPORT_VERBOSE false
#define MSG_PREFIX "-- PROGRAM_BASE: "

namespace base
{
inline VKAPI_ATTR VkBool32 VKAPI_CALL
debug_report_callback(VkDebugReportFlagsEXT msg_flags,
                      VkDebugReportObjectTypeEXT obj_type,
                      uint64_t object,
                      size_t location,
                      int32_t msg_code,
                      const char *layer_prefix,
                      const char *msg,
                      void *pUserData)
{
    std::string msg_prefix;
    uint32_t priority;
    if (msg_flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        msg_prefix = "ERROR";
        priority = 0;
    } else if (msg_flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        msg_prefix = "WARNING";
        priority = 1;
    } else if (msg_flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        msg_prefix = "INFORMATION";
        priority = 2;
    } else if (msg_flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        msg_prefix = "PERFORMANCE";
        priority = 3;
    } else if (msg_flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        msg_prefix = "DEBUG";
        priority = 4;
    } else {
        return VK_FALSE;
    }
    std::ostream &st = priority == 0 ? std::cerr : std::cout;
    st << ">> " << msg_prefix <<
        " | layer " << layer_prefix <<
        " | code " << msg_code << ":\n" <<
        msg << "\n";
    return priority == 0 ? VK_TRUE : VK_FALSE;
}

class Program_base
{
public:
    Program_base(bool enable_validation,
                 Prog_info_base *p_info,
                 Shell_base *p_shell) :
        enable_validation_(enable_validation),
        p_info_(p_info),
        p_shell_(p_shell)
    {
        std::cout << MSG_PREFIX << "enable validation: " << (enable_validation_ ? "true" : "false") << std::endl;
    }

    virtual ~Program_base()
    {
        p_dev_->dev.waitIdle();

        delete p_dev_;
        delete p_phy_dev_;

        instance_.destroySurfaceKHR(surface_);
        p_shell_->destroy_window();

        if (enable_validation_) destroy_debug_report_callback_();
        instance_.destroy();
    }

    void init_base(vk::Format format = vk::Format::eR8G8B8A8Unorm)
    {
        req_inst_extensions_.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        req_inst_extensions_.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        req_device_extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        if (enable_validation_) {
            req_inst_layers_.push_back("VK_LAYER_LUNARG_standard_validation");
            req_inst_extensions_.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        }

        if (!check_instance_layer_support_()) {
            std::string errstr = MSG_PREFIX;
            errstr.append("missing instance layer support");
            throw std::runtime_error(errstr);
        }

        init_vk_();
        init_debug_report_();

        p_phy_dev_ = new Physical_device(&instance_,
                                         p_shell_,
                                         req_phy_dev_features_,
                                         req_device_extensions_);
        p_dev_ = new Device(p_phy_dev_);
        p_shell_->init_window();
        init_surface_(format);
    }

    void run()
    {
        Timer timer;
        double prev_time = timer.get();

        while (true) {
            bool quit = false;
            MSG msg{};
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    quit = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (quit) break;

            acquire_back_buffer_();

            double curr_time = timer.get();
            double delta_time = curr_time - prev_time;
            fps_counter_.update(delta_time);
            prev_time = curr_time;

            present_back_buffer_(curr_time, delta_time);
        }
    }

protected:
    bool enable_validation_;
    Prog_info_base * p_info_;
    Shell_base *p_shell_;
    FPS_counter fps_counter_;

    std::vector<const char *> req_inst_layers_{};
    std::vector<const char *> req_inst_extensions_{};
    vk::PhysicalDeviceFeatures req_phy_dev_features_{};
    std::vector<const char *> req_device_extensions_{};

    vk::Instance instance_;
    VkDebugReportCallbackEXT debug_report_ = VK_NULL_HANDLE;

    Physical_device *p_phy_dev_ = nullptr;
    Device *p_dev_ = nullptr;

    vk::SurfaceKHR surface_;
    vk::SurfaceFormatKHR surface_format_{};

    virtual void acquire_back_buffer_() = 0;
    virtual void present_back_buffer_(float elapsed_time, float delta_time) = 0;

    bool check_instance_layer_support_()
    {
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        for (const char *layer_name : req_inst_layers_) {
            bool found = false;
            for (const auto &layer_props : available_layers) {
                if (strcmp(layer_name, layer_props.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cout << MSG_PREFIX << layer_name << " not supported" << std::endl;
                return false;
            }
        }
        return true;
    }

    void init_vk_()
    {
        vk::ApplicationInfo app_info(p_info_->prog_name().c_str(),
                                     1,
                                     p_info_->prog_name().c_str(),
                                     1,
                                     VK_API_VERSION_1_0);
        vk::InstanceCreateInfo inst_info({},
                                         &app_info,
                                         static_cast<uint32_t>(req_inst_layers_.size()),
                                         req_inst_layers_.data(),
                                         static_cast<uint32_t>(req_inst_extensions_.size()),
                                         req_inst_extensions_.data());
        instance_ = vk::createInstance(inst_info);
    }

    void init_debug_report_()
    {
        if (enable_validation_) {
            VkDebugReportCallbackCreateInfoEXT debug_report_info = {};
            debug_report_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_report_info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT;
            if (DEBUG_REPORT_VERBOSE) {
                debug_report_info.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
            }
            debug_report_info.pfnCallback = debug_report_callback;
            debug_report_info.pUserData = nullptr;
            assert_success(create_debug_report_callback_(&debug_report_info));
        }
    }

    VkResult create_debug_report_callback_(VkDebugReportCallbackCreateInfoEXT *p_create_info)
    {
        auto func = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(VkInstance(instance_), "vkCreateDebugReportCallbackEXT"));
        if (func != nullptr) {
            return func(VkInstance(instance_), p_create_info, nullptr, &debug_report_);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void destroy_debug_report_callback_()
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(VkInstance(instance_), "vkDestroyDebugReportCallbackEXT"));
        if (func != nullptr) {
            func(VkInstance(instance_), debug_report_, nullptr);
        }
    }

    void init_surface_(vk::Format format)
    {
        vk::Win32SurfaceCreateInfoKHR surface_info({}, p_shell_->hinstance, p_shell_->hwnd);
        surface_ = instance_.createWin32SurfaceKHR(surface_info);
        VkBool32 supported;
        p_phy_dev_->phy_dev.getSurfaceSupportKHR(p_phy_dev_->present_queue_family_idx, surface_, &supported);
        assert(supported);

        std::vector<vk::SurfaceFormatKHR> surface_formats = p_phy_dev_->phy_dev.getSurfaceFormatsKHR(surface_);
        assert(!surface_formats.empty());
        if ((surface_formats.size() == 1) && (surface_formats[0].format == vk::Format::eUndefined)) {
            surface_format_ = {format, surface_formats[0].colorSpace};
        } else {
            for (auto &surface_format : surface_formats) {
                if (surface_format.format == format) {
                    surface_format_ = surface_format;
                    return;
                }
            }
            surface_format_ = surface_formats[0];
        }
    }
};
} // namespace base

#undef MSG_PREFIX
