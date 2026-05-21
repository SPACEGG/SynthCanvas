/*
 * ClapSawDemo
 * https://github.com/surge-synthesizer/clap-saw-demo
 *
 * Copyright 2022 Paul Walker and others as listed in the git history
 *
 * Released under the MIT License. See LICENSE.md for full text.
 */

#ifndef CLAP_SAW_DEMO_H
#define CLAP_SAW_DEMO_H
#include <iostream>
#include "debug-helpers.h"

#include <clap/helpers/plugin.hh>
#include <atomic>
#include <array>
#include <unordered_map>
#include <memory>
#include <readerwriterqueue.h>

#include "saw-voice.h"
#include <memory>

namespace sst::clap_saw_demo
{

struct ClapSawDemoEditor;

struct ClapSawDemo : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                  clap::helpers::CheckingLevel::Maximal>
{
    static constexpr int max_voices = 64;
    ClapSawDemo(const clap_host *host);
    ~ClapSawDemo();

    static clap_plugin_descriptor desc;

    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        for (auto &v : voices)
            v.sampleRate = sampleRate;
        return true;
    }

    enum paramIds : uint32_t
    {
        pmUnisonCount = 1378,
        pmUnisonSpread = 2391,
        pmOscDetune = 8675309,

        pmAmpAttack = 2874,
        pmAmpRelease = 728,
        pmAmpIsGate = 1942,

        pmPreFilterVCA = 87612,

        pmCutoff = 17,
        pmResonance = 94,
        pmFilterMode = 14255
    };
    static constexpr int nParams = 10;

    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id paramId) const noexcept override
    {
        return paramToValue.find(paramId) != paramToValue.end();
    }
    uint32_t paramsCount() const noexcept override { return nParams; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override;
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        *value = *paramToValue[paramId];
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override;

  protected:
    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override;

  public:
    // Convert 0-1 linear into 0-4s exponential
    float scaleTimeParamToSeconds(float param);
    float scaleSecondsToTimeParam(float seconds);

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return isInput ? 0 : 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override;

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return isInput ? 1 : 0; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override;

    bool implementsVoiceInfo() const noexcept override { return true; }
    bool voiceInfoGet(clap_voice_info *info) noexcept override
    {
        info->voice_capacity = max_voices;
        info->voice_count = max_voices;
        info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
        return true;
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *) noexcept override;
    bool stateLoad(const clap_istream *) noexcept override;

    clap_process_status process(const clap_process *process) noexcept override;
    void handleInboundEvent(const clap_event_header_t *evt);
    void pushParamsToVoices();
    void handleNoteOn(int port_index, int channel, int key, int noteid);
    void handleNoteOff(int port_index, int channel, int key);
    void handleNoteChoke(int port_index, int channel, int key, int noteid);
    void activateVoice(SawDemoVoice &v, int port_index, int channel, int key, int noteid);
    void handleEventsFromUIQueue(const clap_output_events_t *);

    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override;

    bool startProcessing() noexcept override
    {
#if HAS_GUI
        dataCopyForUI.isProcessing = true;
        dataCopyForUI.updateCount++;
#endif
        return true;
    }
    void stopProcessing() noexcept override
    {
#if HAS_GUI
        dataCopyForUI.isProcessing = false;
        dataCopyForUI.updateCount++;
#endif
    }

  protected:
#if HAS_GUI
    bool implementsGui() const noexcept override { return true; }
    bool guiIsApiSupported(const char *api, bool isFloating) noexcept override;

    bool guiCreate(const char *api, bool isFloating) noexcept override;
    void guiDestroy() noexcept override;
    bool guiSetParent(const clap_window *window) noexcept override;

    bool guiSetScale(double scale) noexcept override;
    bool guiCanResize() const noexcept override { return true; }
    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override;
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override;
    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override;

    std::atomic<bool> refreshUIValues{false};
#endif

    void editorParamsFlush();

#if IS_LINUX && HAS_GUI
  public:
    bool implementsTimerSupport() const noexcept override
    {
        _DBGMARK;
        return true;
    }
    void onTimer(clap_id timerId) noexcept override;

    bool registerTimer(uint32_t interv, clap_id *id);
    bool unregisterTimer(clap_id id);

    bool implementsPosixFdSupport() const noexcept override
    {
        _DBGMARK;
        return true;
    }
    void onPosixFd(int fd, clap_posix_fd_flags_t flags) noexcept override;
    bool registerPosixFd(int fd);
    bool unregisterPosixFD(int fd);

#endif

  public:
#if HAS_GUI
    static constexpr uint32_t GUI_DEFAULT_W = 390, GUI_DEFAULT_H = 530;

    struct ToUI
    {
        enum MType
        {
            PARAM_VALUE = 0x31,
            MIDI_NOTE_ON,
            MIDI_NOTE_OFF
        } type;

        uint32_t id;
        double value;
    };

    struct FromUI
    {
        enum MType
        {
            BEGIN_EDIT = 0xF9,
            END_EDIT,
            ADJUST_VALUE
        } type;
        uint32_t id;
        double value;
    };

    struct DataCopyForUI
    {
        std::atomic<uint32_t> updateCount{0};
        std::atomic<bool> isProcessing{false};
        std::atomic<int> polyphony{0};
        std::atomic<double> tempo{0};
        std::atomic<int> tsNum{0}, tsDen{0};
        std::atomic<double> songpos{0};
    } dataCopyForUI;

    typedef moodycamel::ReaderWriterQueue<ToUI, 4096> SynthToUI_Queue_t;
    typedef moodycamel::ReaderWriterQueue<FromUI, 4096> UIToSynth_Queue_t;

    SynthToUI_Queue_t toUiQ;
    UIToSynth_Queue_t fromUiQ;

  private:
    ClapSawDemoEditor *editor{nullptr};
#endif

    // These parameters now hold NORMALIZED values (0.0 to 1.0) where applicable.
    // Stepped parameters hold discrete values.
    double unisonCount{3}, unisonSpread{0.1}, oscDetune{0.5}, cutoff{69.0/126.0}, resonance{0.7},
        ampAttack{0.01}, ampRelease{0.2}, ampIsGate{0}, preFilterVCA{1.0}, filterMode{0};
    std::unordered_map<clap_id, double *> paramToValue;

    // Modulation offsets (Normalized)
    std::unordered_map<clap_id, double> paramModOffsets;

    std::array<SawDemoVoice, max_voices> voices;
    std::vector<std::tuple<int, int, int, int>> terminatedVoices;
};
} // namespace sst::clap_saw_demo

#endif
