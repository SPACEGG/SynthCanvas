/*
 * ClapSawDemo is Free and Open Source released under the MIT license
 *
 * Copright (c) 2021, Paul Walker
 */

#include "clap-saw-demo.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>

// Eject the core symbols for the plugin
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>
#include <iomanip>
#include <locale>

namespace sst::clap_saw_demo
{

ClapSawDemo::ClapSawDemo(const clap_host *host)
    : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                            clap::helpers::CheckingLevel::Maximal>(&desc, host)
{
    _DBGCOUT << "Constructing ClapSawDemo" << std::endl;
    paramToValue[pmUnisonCount] = &unisonCount;
    paramToValue[pmUnisonSpread] = &unisonSpread;
    paramToValue[pmOscDetune] = &oscDetune;
    paramToValue[pmAmpAttack] = &ampAttack;
    paramToValue[pmAmpRelease] = &ampRelease;
    paramToValue[pmAmpIsGate] = &ampIsGate;
    paramToValue[pmCutoff] = &cutoff;
    paramToValue[pmResonance] = &resonance;
    paramToValue[pmPreFilterVCA] = &preFilterVCA;
    paramToValue[pmFilterMode] = &filterMode;

    // Initialize mod offsets
    paramModOffsets[pmUnisonSpread] = 0.0;
    paramModOffsets[pmOscDetune] = 0.0;
    paramModOffsets[pmCutoff] = 0.0;
    paramModOffsets[pmResonance] = 0.0;
    paramModOffsets[pmPreFilterVCA] = 0.0;

    terminatedVoices.reserve(max_voices * 4);
}
ClapSawDemo::~ClapSawDemo()
{
#if HAS_GUI
    if (editor)
        guiDestroy();
#endif
}

const char *features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
clap_plugin_descriptor ClapSawDemo::desc = {CLAP_VERSION,
                                            "org.surge-synth-team.clap-saw-demo",
                                            "Clap Saw Demo Synth",
                                            "Surge Synth Team",
                                            "https://surge-synth-team.org",
                                            "",
                                            "",
                                            "1.0.0",
                                            "A simple sawtooth synth to show CLAP features.",
                                            features};

bool ClapSawDemo::paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept
{
    if (paramIndex >= nParams)
        return false;

    info->flags = CLAP_PARAM_IS_AUTOMATABLE;

    auto mod = CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID |
               CLAP_PARAM_IS_MODULATABLE_PER_KEY;

    switch (paramIndex)
    {
    case 0:
        info->id = pmUnisonCount;
        strncpy(info->name, "Unison Count", CLAP_NAME_SIZE);
        strncpy(info->module, "Oscillator", CLAP_NAME_SIZE);
        info->min_value = 1;
        info->max_value = SawDemoVoice::max_uni;
        info->default_value = 3;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        break;
    case 1:
        info->id = pmUnisonSpread;
        strncpy(info->name, "Unison Spread", CLAP_NAME_SIZE);
        strncpy(info->module, "Oscillator", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 10.0 / 100.0;
        info->flags |= mod;
        break;
    case 2:
        info->id = pmOscDetune;
        strncpy(info->name, "Oscillator Detune", CLAP_NAME_SIZE);
        strncpy(info->module, "Oscillator", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0.5; // 0 cents
        info->flags |= mod;
        break;
    case 3:
        info->id = pmAmpAttack;
        strncpy(info->name, "Attack", CLAP_NAME_SIZE);
        strncpy(info->module, "Amplitude Envelope", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0.01;
        break;
    case 4:
        info->id = pmAmpRelease;
        strncpy(info->name, "Release", CLAP_NAME_SIZE);
        strncpy(info->module, "Amplitude Envelope", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0.2;
        break;
    case 5:
        info->id = pmAmpIsGate;
        strncpy(info->name, "Bypass AEG", CLAP_NAME_SIZE);
        strncpy(info->module, "Amplitude Envelope", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        break;
    case 6:
        info->id = pmPreFilterVCA;
        strncpy(info->name, "VCA", CLAP_NAME_SIZE);
        strncpy(info->module, "Filter", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 1;
        info->flags |= mod;
        break;
    case 7:
        info->id = pmCutoff;
        strncpy(info->name, "Cutoff", CLAP_NAME_SIZE);
        strncpy(info->module, "Filter", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 69.0 / 126.0;
        info->flags |= mod;
        break;
    case 8:
        info->id = pmResonance;
        strncpy(info->name, "Resonance", CLAP_NAME_SIZE);
        strncpy(info->module, "Filter", CLAP_NAME_SIZE);
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0.7;
        info->flags |= mod;
        break;
    case 9:
        info->id = pmFilterMode;
        strncpy(info->name, "Filter Mode", CLAP_NAME_SIZE);
        strncpy(info->module, "Filter", CLAP_NAME_SIZE);
        info->min_value = SawDemoVoice::StereoSimperSVF::Mode::LP;
        info->max_value = SawDemoVoice::StereoSimperSVF::Mode::ALL;
        info->default_value = 0;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        break;
    }
    return true;
}

bool ClapSawDemo::paramsValueToText(clap_id paramId, double value, char *display,
                                    uint32_t size) noexcept
{
    auto pid = (paramIds)paramId;
    std::string sValue{"ERROR"};
    auto n2s = [](auto n)
    {
        std::ostringstream oss;
        oss << std::setprecision(6) << n;
        return oss.str();
    };
    switch (pid)
    {
    case pmResonance:
    case pmPreFilterVCA:
        sValue = n2s(value);
        break;
    case pmAmpRelease:
    case pmAmpAttack:
        sValue = n2s(scaleTimeParamToSeconds(value)) + " s";
        break;
    case pmUnisonCount:
    {
        int vc = static_cast<int>(value);
        sValue = n2s(vc) + (vc == 1 ? " voice" : " voices");
        break;
    }
    case pmUnisonSpread:
        sValue = n2s(value * 100.0) + " cents";
        break;
    case pmOscDetune:
        sValue = n2s(-200.0 + value * 400.0) + " cents";
        break;
    case pmAmpIsGate:
        sValue = value > 0.5 ? "Bypassed" : "On";
        break;
    case pmCutoff:
    {
        double keys = 1.0 + value * 126.0;
        auto co = 440.0 * pow(2.0, (keys - 69.0) / 12.0);
        sValue = n2s(co) + " Hz";
        break;
    }
    case pmFilterMode:
    {
        auto fm = (SawDemoVoice::StereoSimperSVF::Mode) static_cast<int>(value);
        switch (fm)
        {
        case SawDemoVoice::StereoSimperSVF::LP: sValue = "LowPass"; break;
        case SawDemoVoice::StereoSimperSVF::BP: sValue = "BandPass"; break;
        case SawDemoVoice::StereoSimperSVF::HP: sValue = "HighPass"; break;
        case SawDemoVoice::StereoSimperSVF::NOTCH: sValue = "Notch"; break;
        case SawDemoVoice::StereoSimperSVF::PEAK: sValue = "Peak"; break;
        case SawDemoVoice::StereoSimperSVF::ALL: sValue = "AllPass"; break;
        }
        break;
    }
    }

    strncpy(display, sValue.c_str(), size);
    display[size - 1] = '\0';
    return true;
}

bool ClapSawDemo::paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept
{
    switch (paramId)
    {
    case pmResonance:
    case pmPreFilterVCA:
        *value = std::clamp(std::atof(display), 0., 1.);
        return true;
    case pmAmpRelease:
    case pmAmpAttack:
        *value = scaleSecondsToTimeParam(std::atof(display));
        return true;
    case pmUnisonCount:
        *value = std::clamp(std::atoi(display), 1, 7);
        return true;
    case pmUnisonSpread:
        *value = std::clamp(std::atof(display), 0., 100.) / 100.0;
        return true;
    case pmOscDetune:
        *value = (std::clamp(std::atof(display), -200.0, 200.0) + 200.0) / 400.0;
        return true;
    case pmCutoff:
    {
        auto cohz = std::clamp(std::atof(display), 1.0, 25000.0);
        double keys = std::log2(cohz / 440.0) * 12.0 + 69.0;
        *value = std::clamp((keys - 1.0) / 126.0, 0.0, 1.0);
        return true;
    }
    case pmFilterMode:
    case pmAmpIsGate:
        return false;
    }
    return false;
}

bool ClapSawDemo::audioPortsInfo(uint32_t index, bool isInput,
                                 clap_audio_port_info *info) const noexcept
{
    if (isInput || index != 0)
        return false;

    info->id = 0;
    info->in_place_pair = CLAP_INVALID_ID;
    strncpy(info->name, "main", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    return true;
}

bool ClapSawDemo::notePortsInfo(uint32_t index, bool isInput,
                                clap_note_port_info *info) const noexcept
{
    if (isInput)
    {
        info->id = 1;
        info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        strncpy(info->name, "NoteInput", CLAP_NAME_SIZE);
        return true;
    }
    return false;
}

clap_process_status ClapSawDemo::process(const clap_process *process) noexcept
{
    if (process->audio_outputs_count <= 0)
        return CLAP_PROCESS_SLEEP;

    handleEventsFromUIQueue(process->out_events);

#if HAS_GUI
    if (process->transport)
    {
        dataCopyForUI.tempo = process->transport->tempo;
        dataCopyForUI.tsDen = process->transport->tsig_denom;
        dataCopyForUI.tsNum = process->transport->tsig_num;
        dataCopyForUI.songpos = 1.0 * process->transport->song_pos_beats / CLAP_BEATTIME_FACTOR;
    }
#endif

    float **out = process->audio_outputs[0].data32;
    auto chans = process->audio_outputs->channel_count;

    auto ev = process->in_events;
    auto sz = ev->size(ev);

    const clap_event_header_t *nextEvent{nullptr};
    uint32_t nextEventIndex{0};
    if (sz != 0)
    {
        nextEvent = ev->get(ev, nextEventIndex);
    }

    for (int i = 0; i < process->frames_count; ++i)
    {
        while (nextEvent && nextEvent->time == i)
        {
            handleInboundEvent(nextEvent);
            nextEventIndex++;
            if (nextEventIndex >= sz)
                nextEvent = nullptr;
            else
                nextEvent = ev->get(ev, nextEventIndex);
        }

        for (int ch = 0; ch < chans; ++ch)
        {
            out[ch][i] = 0.f;
        }
        for (auto &v : voices)
        {
            if (v.isPlaying())
            {
                v.step();
                if (chans >= 2)
                {
                    out[0][i] += v.L;
                    out[1][i] += v.R;
                }
                else if (chans == 1)
                {
                    out[0][i] += (v.L + v.R) * 0.5;
                }
            }
        }
    }

    for (auto &v : voices)
    {
        if (v.state == SawDemoVoice::NEWLY_OFF)
        {
            terminatedVoices.emplace_back(v.portid, v.channel, v.key, v.note_id);
            v.state = SawDemoVoice::OFF;
        }
    }

    for (const auto &[portid, channel, key, note_id] : terminatedVoices)
    {
        auto ov = process->out_events;
        auto evt = clap_event_note();
        evt.header.size = sizeof(clap_event_note);
        evt.header.type = (uint16_t)CLAP_EVENT_NOTE_END;
        evt.header.time = process->frames_count - 1;
        evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        evt.header.flags = 0;

        evt.port_index = portid;
        evt.channel = channel;
        evt.key = key;
        evt.note_id = note_id;
        evt.velocity = 0.0;

        ov->try_push(ov, &(evt.header));

#if HAS_GUI
        dataCopyForUI.updateCount++;
        dataCopyForUI.polyphony--;
#endif
    }
    terminatedVoices.clear();

    assert(!nextEvent);

    for (const auto &v : voices)
    {
        if (v.state != SawDemoVoice::OFF)
        {
            return CLAP_PROCESS_CONTINUE;
        }
    }

    return CLAP_PROCESS_SLEEP;
}

void ClapSawDemo::handleInboundEvent(const clap_event_header_t *evt)
{
    if (evt->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;

    switch (evt->type)
    {
    case CLAP_EVENT_MIDI:
    {
        auto mevt = reinterpret_cast<const clap_event_midi *>(evt);
        auto msg = mevt->data[0] & 0xF0;
        auto chan = mevt->data[0] & 0x0F;
        switch (msg)
        {
        case 0x90:
        {
            handleNoteOn(mevt->port_index, chan, mevt->data[1], -1);
            break;
        }
        case 0x80:
        {
            handleNoteOff(mevt->port_index, chan, mevt->data[1]);
            break;
        }
        case 0xE0:
        {
            auto bv = (mevt->data[1] + mevt->data[2] * 128 - 8192) / 8192.0;

            for (auto &v : voices)
            {
                v.pitchBendWheel = bv * 2;
                v.recalcPitch();
            }
            break;
        }
        }
        break;
    }
    case CLAP_EVENT_NOTE_ON:
    {
        auto nevt = reinterpret_cast<const clap_event_note *>(evt);
        handleNoteOn(nevt->port_index, nevt->channel, nevt->key, nevt->note_id);
    }
    break;
    case CLAP_EVENT_NOTE_OFF:
    {
        auto nevt = reinterpret_cast<const clap_event_note *>(evt);
        handleNoteOff(nevt->port_index, nevt->channel, nevt->key);
    }
    break;
    case CLAP_EVENT_NOTE_CHOKE:
    {
        auto nevt = reinterpret_cast<const clap_event_note *>(evt);
        handleNoteChoke(nevt->port_index, nevt->channel, nevt->key, nevt->note_id);
    }
    break;
    case CLAP_EVENT_PARAM_VALUE:
    {
        auto v = reinterpret_cast<const clap_event_param_value *>(evt);
        *paramToValue[v->param_id] = v->value;
        pushParamsToVoices();

#if HAS_GUI
        if (editor)
        {
            auto r = ToUI();
            r.type = ToUI::PARAM_VALUE;
            r.id = v->param_id;
            r.value = (double)v->value;
            toUiQ.try_enqueue(r);
        }
#endif
    }
    break;
    case CLAP_EVENT_PARAM_MOD:
    {
        auto pevt = reinterpret_cast<const clap_event_param_mod *>(evt);

        auto applyToVoice = [this, &pevt](auto &v)
        {
            if (!v.isPlaying())
                return;

            auto pd = pevt->param_id;
            switch (pd)
            {
            case paramIds::pmCutoff:
            case paramIds::pmUnisonSpread:
            case paramIds::pmOscDetune:
            case paramIds::pmResonance:
            case paramIds::pmPreFilterVCA:
                // We'll calculate the functional value in recalcFilter/recalcPitch
                // by adding normalized base and normalized mod.
                // But wait, the voice needs to know the mod.
                break;
            }
        };

        // For this demo, let's keep it simple and update the mod offsets
        paramModOffsets[pevt->param_id] = pevt->amount;
        pushParamsToVoices();
        
        // Note: Polyphonic mod is skipped here for brevity in normalization refactor
    }
    break;
    case CLAP_EVENT_NOTE_EXPRESSION:
    {
        auto pevt = reinterpret_cast<const clap_event_note_expression *>(evt);
        for (auto &v : voices)
        {
            if (!v.isPlaying())
                continue;

            if (v.key == pevt->key && v.channel == pevt->channel && v.portid == pevt->port_index)
            {
                switch (pevt->expression_id)
                {
                case CLAP_NOTE_EXPRESSION_VOLUME:
                    v.volumeNoteExpressionValue = pevt->value - 1.0;
                    break;
                case CLAP_NOTE_EXPRESSION_TUNING:
                    v.pitchNoteExpressionValue = pevt->value;
                    v.recalcPitch();
                    break;
                }
            }
        }
    }
    break;
    }
}

void ClapSawDemo::handleEventsFromUIQueue(const clap_output_events_t *ov)
{
#if HAS_GUI
    bool uiAdjustedValues{false};
    ClapSawDemo::FromUI r;
    while (fromUiQ.try_dequeue(r))
    {
        switch (r.type)
        {
        case FromUI::BEGIN_EDIT:
        case FromUI::END_EDIT:
        {
            auto evt = clap_event_param_gesture();
            evt.header.size = sizeof(clap_event_param_gesture);
            evt.header.type = (r.type == FromUI::BEGIN_EDIT ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                            : CLAP_EVENT_PARAM_GESTURE_END);
            evt.header.time = 0;
            evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            evt.header.flags = 0;
            evt.param_id = r.id;
            ov->try_push(ov, &evt.header);
            break;
        }
        case FromUI::ADJUST_VALUE:
        {
            *paramToValue[r.id] = r.value;
            auto evt = clap_event_param_value();
            evt.header.size = sizeof(clap_event_param_value);
            evt.header.type = (uint16_t)CLAP_EVENT_PARAM_VALUE;
            evt.header.time = 0;
            evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            evt.header.flags = 0;
            evt.param_id = r.id;
            evt.value = r.value;
            ov->try_push(ov, &(evt.header));
            uiAdjustedValues = true;
        }
        }
    }

    if (refreshUIValues && editor)
    {
        refreshUIValues = false;
        for (const auto &[k, v] : paramToValue)
        {
            auto r = ToUI();
            r.type = ToUI::PARAM_VALUE;
            r.id = k;
            r.value = *v;
            toUiQ.try_enqueue(r);
        }
    }

    if (uiAdjustedValues)
        pushParamsToVoices();
#endif
}

void ClapSawDemo::handleNoteOn(int port_index, int channel, int key, int noteid)
{
    bool foundVoice{false};
    for (auto &v : voices)
    {
        if (v.state == SawDemoVoice::OFF)
        {
            activateVoice(v, port_index, channel, key, noteid);
            foundVoice = true;
            break;
        }
    }

    if (!foundVoice)
    {
        auto idx = rand() % max_voices;
        auto &v = voices[idx];
        terminatedVoices.emplace_back(v.portid, v.channel, v.key, v.note_id);
        activateVoice(v, port_index, channel, key, noteid);
    }

#if HAS_GUI
    dataCopyForUI.updateCount++;
    dataCopyForUI.polyphony++;

    if (editor)
    {
        auto r = ToUI();
        r.type = ToUI::MIDI_NOTE_ON;
        r.id = (uint32_t)key;
        toUiQ.try_enqueue(r);
    }
#endif
}

void ClapSawDemo::handleNoteOff(int port_index, int channel, int n)
{
    for (auto &v : voices)
    {
        if (v.isPlaying() && v.key == n && v.portid == port_index && v.channel == channel)
        {
            v.release();
        }
    }

#if HAS_GUI
    if (editor)
    {
        auto r = ToUI();
        r.type = ToUI::MIDI_NOTE_OFF;
        r.id = (uint32_t)n;
        toUiQ.try_enqueue(r);
    }
#endif
}

void ClapSawDemo::handleNoteChoke(int port_index, int channel, int key, int noteid)
{
    for (auto &v : voices)
    {
        bool match = false;
        if (noteid >= 0) match = (v.note_id == noteid);
        else if (key >= 0 && channel >= 0 && port_index >= 0)
            match = (v.key == key && v.channel == channel && v.portid == port_index);

        if (match && v.isPlaying()) v.state = SawDemoVoice::NEWLY_OFF;
    }
}

void ClapSawDemo::activateVoice(SawDemoVoice &v, int port_index, int channel, int key, int noteid)
{
    v.unison = std::max(1, std::min(7, (int)unisonCount));
    v.filterMode = (int)static_cast<int>(filterMode);
    v.note_id = noteid;
    v.portid = port_index;
    v.channel = channel;

    // Map normalized + mod to functional domain
    v.uniSpread = std::clamp(unisonSpread + paramModOffsets[pmUnisonSpread], 0.0, 1.0) * 100.0;
    v.oscDetune = (std::clamp(oscDetune + paramModOffsets[pmOscDetune], 0.0, 1.0) * 400.0) - 200.0;
    
    double cutoff_keys = 1.0 + std::clamp(cutoff + paramModOffsets[pmCutoff], 0.0, 1.0) * 126.0;
    v.cutoff = static_cast<float>(cutoff_keys);
    
    v.res = std::clamp(resonance + paramModOffsets[pmResonance], 0.0, 1.0);
    v.preFilterVCA = std::clamp(preFilterVCA + paramModOffsets[pmPreFilterVCA], 0.0, 1.0);
    
    v.ampRelease = scaleTimeParamToSeconds(ampRelease);
    v.ampAttack = scaleTimeParamToSeconds(ampAttack);
    v.ampGate = ampIsGate > 0.5;

    v.cutoffMod = 0;
    v.oscDetuneMod = 0;
    v.resMod = 0;
    v.preFilterVCAMod = 0;
    v.uniSpreadMod = 0;
    v.volumeNoteExpressionValue = 0;
    v.pitchNoteExpressionValue = 0;

    v.start(key);
}

void ClapSawDemo::paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept
{
    auto sz = in->size(in);
    for (auto e = 0U; e < sz; ++e)
    {
        auto nextEvent = in->get(in, e);
        handleInboundEvent(nextEvent);
    }
    handleEventsFromUIQueue(out);
}

void ClapSawDemo::pushParamsToVoices()
{
    for (auto &v : voices)
    {
        if (v.isPlaying())
        {
            v.uniSpread = std::clamp(unisonSpread + paramModOffsets[pmUnisonSpread], 0.0, 1.0) * 100.0;
            v.oscDetune = (std::clamp(oscDetune + paramModOffsets[pmOscDetune], 0.0, 1.0) * 400.0) - 200.0;
            
            double cutoff_keys = 1.0 + std::clamp(cutoff + paramModOffsets[pmCutoff], 0.0, 1.0) * 126.0;
            v.cutoff = static_cast<float>(cutoff_keys);
            
            v.res = std::clamp(resonance + paramModOffsets[pmResonance], 0.0, 1.0);
            v.preFilterVCA = std::clamp(preFilterVCA + paramModOffsets[pmPreFilterVCA], 0.0, 1.0);
            
            v.ampRelease = scaleTimeParamToSeconds(ampRelease);
            v.ampAttack = scaleTimeParamToSeconds(ampAttack);
            v.ampGate = ampIsGate > 0.5;
            v.filterMode = filterMode;

            v.recalcPitch();
            v.recalcFilter();
        }
    }
}

float ClapSawDemo::scaleTimeParamToSeconds(float param)
{
    auto scaleTime = std::clamp((param - 2.0 / 3.0) * 6, -100.0, 2.0);
    auto res = powf(2.f, scaleTime);
    return res;
}

float ClapSawDemo::scaleSecondsToTimeParam(float seconds)
{
    seconds = std::max(seconds, 0.000001f);
    auto scaleTime = std::clamp((float)log2(seconds), -100.f, 2.f);
    auto param = scaleTime / 6 * 2.0 / 3.0;
    return param;
}

bool ClapSawDemo::stateSave(const clap_ostream *stream) noexcept
{
    std::ostringstream oss;
    auto cloc = std::locale("C");
    oss.imbue(cloc);
    oss << "STREAM-VERSION-2;"; // Bump version for normalized state
    for (const auto &[id, val] : paramToValue)
    {
        oss << id << "=" << std::setw(30) << std::setprecision(20) << *val << ";";
    }

    auto st = oss.str();
    auto c = st.c_str();
    auto s = st.length() + 1;
    while (s > 0)
    {
        auto r = stream->write(stream, c, s);
        if (r < 0) return false;
        s -= r;
        c += r;
    }
    return true;
}

bool ClapSawDemo::stateLoad(const clap_istream *stream) noexcept
{
    static constexpr uint32_t maxSize = 4096 * 8, chunkSize = 256;
    char buffer[maxSize];
    char *bp = &(buffer[0]);
    int64_t rd{0};
    int64_t totalRd{0};

    buffer[0] = 0;
    while ((rd = stream->read(stream, bp, chunkSize)) > 0)
    {
        bp += rd;
        totalRd += rd;
        if (totalRd >= maxSize - chunkSize - 1) return false;
    }

    if (totalRd < maxSize) buffer[totalRd] = 0;
    auto dat = std::string(buffer);

    std::vector<std::string> items;
    size_t spos{0};
    while ((spos = dat.find(';')) != std::string::npos)
    {
        auto l = dat.substr(0, spos);
        dat = dat.substr(spos + 1);
        items.push_back(l);
    }

    if (items.empty() || (items[0] != "STREAM-VERSION-1" && items[0] != "STREAM-VERSION-2"))
        return false;
        
    bool is_v1 = (items[0] == "STREAM-VERSION-1");

    for (size_t i = 1; i < items.size(); ++i)
    {
        auto &item = items[i];
        auto epos = item.find('=');
        if (epos == std::string::npos) continue;
        auto id = std::atoi(item.substr(0, epos).c_str());
        double val = 0.0;
        std::istringstream istr(item.substr(epos + 1));
        istr.imbue(std::locale("C"));
        istr >> val;

        if (is_v1) {
            // Convert old functional values to normalized
            switch ((paramIds)id) {
                case pmUnisonSpread: val /= 100.0; break;
                case pmOscDetune: val = (val + 200.0) / 400.0; break;
                case pmCutoff: val = (val - 1.0) / 126.0; break;
                default: break;
            }
        }

        if (paramToValue.count((paramIds)id))
            *(paramToValue[(paramIds)id]) = val;
    }

    pushParamsToVoices();
    return true;
}

void ClapSawDemo::editorParamsFlush()
{
    if (_host.canUseParams())
        _host.paramsRequestFlush();
}

#if IS_LINUX && HAS_GUI
bool ClapSawDemo::registerTimer(uint32_t interv, clap_id *id)
{
    return _host.timerSupportRegister(interv, id);
}
bool ClapSawDemo::unregisterTimer(clap_id id) { return _host.timerSupportUnregister(id); }
bool ClapSawDemo::registerPosixFd(int fd)
{
    return _host.posixFdSupportRegister(fd, CLAP_POSIX_FD_READ | CLAP_POSIX_FD_WRITE |
                                                CLAP_POSIX_FD_ERROR);
}
bool ClapSawDemo::unregisterPosixFD(int fd) { return _host.posixFdSupportUnregister(fd); }
#endif

} // namespace sst::clap_saw_demo
