// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace cowtype {
// Platform-only control endpoint. It never receives or publishes input text.
class InputBus {
    class Object final : public fcitx::dbus::ObjectVTable<Object> {
        std::function<std::string()> mode_;
        std::function<std::string(const std::string &)> setMode_;
        std::string version_;
    public:
        Object(std::function<std::string()> mode,
               std::function<std::string(const std::string &)> setMode,
               std::string version)
            : mode_(std::move(mode)), setMode_(std::move(setMode)), version_(std::move(version)) {}
        std::string mode() { return mode_(); }
        std::string setMode(const std::string &value) {
            if(value!="zh" && value!="en" && value!="toggle")
                throw fcitx::dbus::MethodCallError("org.freedesktop.DBus.Error.InvalidArgs","Expected zh, en or toggle");
            return setMode_(value);
        }
        std::string version() const { return version_; }
        FCITX_OBJECT_VTABLE_METHOD(mode,"Mode","","s");
        FCITX_OBJECT_VTABLE_METHOD(setMode,"SetMode","s","s");
        FCITX_OBJECT_VTABLE_METHOD(version,"Version","","s");
    };
    // Object unregisters before the connection is destroyed.
    std::unique_ptr<fcitx::dbus::Bus> bus_;
    std::unique_ptr<Object> object_;
public:
    InputBus(fcitx::EventLoop &loop, std::function<std::string()> mode,
             std::function<std::string(const std::string &)> setMode,
             const std::string &version) {
        bus_=std::make_unique<fcitx::dbus::Bus>(fcitx::dbus::BusType::Session);
        object_=std::make_unique<Object>(std::move(mode),std::move(setMode),version);
        if(!bus_->isOpen() || !bus_->addObjectVTable("/org/cowtype/Input","org.cowtype.Input1",*object_) ||
           !bus_->requestName("org.cowtype.Input",fcitx::dbus::RequestNameFlag::None))
            throw std::runtime_error("CowType D-Bus registration failed");
        bus_->attachEventLoop(&loop);
    }
};
} // namespace cowtype
