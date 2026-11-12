#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <juce_events/juce_events.h>
#include <memory>

// JUCE needs a message manager for AudioProcessor/APVTS in tests; tear it down cleanly before exit().
struct JuceListener : Catch::EventListenerBase
{
    using EventListenerBase::EventListenerBase;
    void testRunStarting (const Catch::TestRunInfo&) override { init = std::make_unique<juce::ScopedJuceInitialiser_GUI>(); }
    void testRunEnded (const Catch::TestRunStats&) override { init.reset(); }
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> init;
};
CATCH_REGISTER_LISTENER (JuceListener)
