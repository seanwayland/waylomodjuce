/*
 ==============================================================================
 
 A simple delay example with time and feedback knobs
 
 It contains the basic framework code for a JUCE plugin processor.
 
 ==============================================================================
 */

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"


//==============================================================================
/**
 */
class TapeDelayAudioProcessor  :  public AudioProcessor,
public AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    TapeDelayAudioProcessor();
    ~TapeDelayAudioProcessor();
    
    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    
    void parameterChanged (const String &parameterID, float newValue) override;
    
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    
    void processBlock (AudioSampleBuffer&, MidiBuffer&) override;
    
    void writeToDelayBuffer (AudioSampleBuffer& delayBuffer, AudioSampleBuffer& buffer,
                             const int channelIn, const int channelOut,
                             const int writePos,
                             float startGain, float endGain,
                             bool replacing);
    
    void readFromDelayBuffer (AudioSampleBuffer& buffer,
                              const int channelIn, const int channelOut,
                              const int readPos,
                              float startGain, float endGain,
                              bool replacing);
    
    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    
    //==============================================================================
    const String getName() const override;
    
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;
    
    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;
    
    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    AudioProcessorValueTreeState& getValueTreeState();
    
    static String paramGain;
    static String paramTimeOne;
    static String paramFeedbackOne;
    static String paramTimeTwo;
    static String paramFeedbackTwo;
    
private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeDelayAudioProcessor)
    
    Atomic<float>   mGain     {   0.0f };
    Atomic<float>   mTimeOne     { 200.0f };
    Atomic<float>   mFeedbackOne {  -6.0f };
    Atomic<float>   mTimeTwo    { 200.0f };
    Atomic<float>   mFeedbackTwo {  -6.0f };
    
    UndoManager                  mUndoManager;
    AudioProcessorValueTreeState mState;
    
    AudioSampleBuffer            mDelayBufferOne;
    AudioSampleBuffer            mDelayBufferTwo;
    
    float mLastInputGainOne    = 0.0f;
    float mLastFeedbackGainOne = 0.0f;
    
    int    mWritePosOne        = 0;
    int    mExpectedReadPosOne = -1;
    double mSampleRate      = 0;
    
    float mLastInputGainTwo    = 0.0f;
    float mLastFeedbackGainTwo = 0.0f;
    
    int    mWritePosTwo        = 0;
    int    mExpectedReadPosTwo = -1;

};
