// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>
#include <string_view>
#include "audio_core/sink/sink.h"

namespace AudioCore::Sink {
class PS4Sink final : public Sink {
public:
    explicit PS4Sink(std::string_view);
    ~PS4Sink() override;
    SinkStream* AcquireSinkStream(Core::System&, u32, const std::string&, StreamType) override;
    void CloseStream(SinkStream*) override;
    void CloseStreams() override;
    f32 GetDeviceVolume() const override { return device_volume; }
    void SetDeviceVolume(f32) override;
    void SetSystemVolume(f32) override;
private:
    std::vector<std::unique_ptr<SinkStream>> streams;
    f32 system_volume{1.0f};
};
}
