# Source-only, idempotent patch for the pinned JUCE 8.0.15 dependency.
# Applied by the user's build configuration; never changes parameter IDs or DSP.
set(_lsampler_flag_file "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/utilities/juce_FlagCache.h")
if(NOT EXISTS "${_lsampler_flag_file}")
    message(FATAL_ERROR "TEST143: expected JUCE 8.0.15 FlagCache header was not found")
endif()
file(READ "${_lsampler_flag_file}" _lsampler_flag_source)
string(REPLACE "\r\n" "\n" _lsampler_flag_source "${_lsampler_flag_source}")
string(SHA256 _lsampler_flag_hash "${_lsampler_flag_source}")
if(_lsampler_flag_hash STREQUAL "76ee931f924c076f922732be0003033eb7f741d41d5c831d5bb8cb0a39ea510f")
    message(STATUS "LSampler TEST143: JUCE empty parameter groups already optimised")
elseif(_lsampler_flag_hash STREQUAL "8eaabac7379992064008ac4c248187a21f642fc842ae02ecead6f06e9cc6cd55")
    set(_lsampler_old [==[            const auto prevFlags = flags[flagIndex].exchange (0, std::memory_order_acq_rel);

            for (size_t group = 0; group < groupsPerWord; ++group)]==])
    set(_lsampler_new [==[            // LSampler TEST143: an empty word needs no locked write or bit scan.
            // A producer racing this load leaves its flags pending for the next
            // drain, just as a producer racing the original exchange did.
            if (flags[flagIndex].load (std::memory_order_acquire) == 0)
                continue;

            const auto prevFlags = flags[flagIndex].exchange (0, std::memory_order_acq_rel);

            for (size_t group = 0; group < groupsPerWord; ++group)]==])
    string(REPLACE "${_lsampler_old}" "${_lsampler_new}" _lsampler_flag_source "${_lsampler_flag_source}")
    string(SHA256 _lsampler_result_hash "${_lsampler_flag_source}")
    if(NOT _lsampler_result_hash STREQUAL "76ee931f924c076f922732be0003033eb7f741d41d5c831d5bb8cb0a39ea510f")
        message(FATAL_ERROR "TEST143: FlagCache patch verification failed")
    endif()
    file(WRITE "${_lsampler_flag_file}" "${_lsampler_flag_source}")
    message(STATUS "LSampler TEST143: JUCE empty parameter groups optimised")
else()
    message(FATAL_ERROR "TEST143: unrecognised JUCE FlagCache source; review before updating JUCE")
endif()
