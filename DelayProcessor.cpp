/*
 ==============================================================================
 
 A simple delay example with time and feedback knobs
 
 It contains the basic framework code for a JUCE plugin processor.
 
 ==============================================================================
 */

#include "TapeDelayProcessor.h"
#include "TapeDelayEditor.h"


String TapeDelayAudioProcessor::paramGain     ("gain");
String TapeDelayAudioProcessor::paramTimeOne     ("time");
String TapeDelayAudioProcessor::paramFeedbackOne ("feedback");


//==============================================================================
TapeDelayAudioProcessor::TapeDelayAudioProcessor()
: mState (*this, &mUndoManager, "FFTapeDelay",
{
    std::make_unique<AudioParameterFloat>(paramGain,
                                          TRANS ("Input Gain"),
                                          NormalisableRange<float>(-100.0f, 6.0f, 0.1f, std::log (0.5f) / std::log (100.0f / 106.0f)),
                                          mGain.get(), "dB",
                                          AudioProcessorParameter::genericParameter,
                                          [](float v, int) { return String (v, 1) + " dB"; },
                                          [](const String& t) { return t.dropLastCharacters (3).getFloatValue(); }),
    std::make_unique<AudioParameterFloat>(paramTimeOne,
                                          TRANS ("Delay TIme"),    NormalisableRange<float>(0.0, 2000.0, 1.0),
                                          mTimeOne.get(), "ms",
                                          AudioProcessorParameter::genericParameter,
                                          [](float v, int) { return String (roundToInt (v)) + " ms"; },
                                          [](const String& t) { return t.dropLastCharacters (3).getFloatValue(); }),
    std::make_unique<AudioParameterFloat>(paramFeedbackOne,
                                          TRANS ("Feedback Gain"), NormalisableRange<float>(-100.0f, 6.0f, 0.1f, std::log (0.5f) / std::log (100.0f / 106.0f)),
                                          mFeedbackOne.get(), "dB", AudioProcessorParameter::genericParameter,
                                          [](float v, int) { return String (v, 1) + " dB"; },
                                          [](const String& t) { return t.dropLastCharacters (3).getFloatValue(); })
})
{
    mState.addParameterListener (paramGain, this);
    mState.addParameterListener (paramTimeOne, this);
    mState.addParameterListener (paramFeedbackOne, this);
}

TapeDelayAudioProcessor::~TapeDelayAudioProcessor()
{
}

//==============================================================================
void TapeDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    mSampleRate = sampleRate;
    
    // sample buffer for 2 seconds + 2 buffers safety
    mDelayBufferOne.setSize (getTotalNumOutputChannels(), 2.0 * (samplesPerBlock + sampleRate), false, false);
    mDelayBufferOne.clear();
    
    mExpectedReadPosOne = -1;
}

void TapeDelayAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

void TapeDelayAudioProcessor::parameterChanged (const String &parameterID, float newValue)
{
    if (parameterID == paramGain) {
        mGain = newValue;
    }
    else if (parameterID == paramTimeOne) {
        mTimeOne = newValue;
    }
    else if (parameterID == paramFeedbackOne) {
        mFeedbackOne = newValue;
    }
}

bool TapeDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // we only support stereo and mono
    if (layouts.getMainInputChannels() == 0 || layouts.getMainInputChannels() > 2)
        return false;
    
    if (layouts.getMainOutputChannels() == 0 || layouts.getMainOutputChannels() > 2)
        return false;
    
    // we don't allow the narrowing the number of channels
    if (layouts.getMainInputChannels() > layouts.getMainOutputChannels())
        return false;
    
    return true;
}

void TapeDelayAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    if (Bus* inputBus = getBus (true, 0))
    {
        const float delayLevelOne = 0.8;
        const float gain = Decibels::decibelsToGain (mGain.get());
        const float timeOne = mTimeOne.get();
        const float feedbackOne = Decibels::decibelsToGain (mFeedbackOne.get());
        
        // write original to delay
        for (int i=0; i < mDelayBufferOne.getNumChannels(); ++i)
        {
            const int inputChannelNum = inputBus->getChannelIndexInProcessBlockBuffer (std::min (i, inputBus->getNumberOfChannels()));
            
            writeToDelayBuffer (mDelayBufferOne, buffer, inputChannelNum, i, mWritePosOne, 1.0f, 1.0f, true);
        }
        
        // adapt dry gain
        buffer.applyGainRamp (0, buffer.getNumSamples(), mLastInputGainOne, gain);
        mLastInputGainOne = gain;
        
        // read delayed signal
        auto readPos = roundToInt (mWritePosOne - (mSampleRate * timeOne / 1000.0));
        if (readPos < 0)
            readPos += mDelayBufferOne.getNumSamples();
        
        if (Bus* outputBus = getBus (false, 0))
        {
            // if has run before
            if (mExpectedReadPosOne >= 0)
            {
                // fade out if readPos is off
                auto endGain = (readPos == mExpectedReadPosOne) ? delayLevelOne : 0.0f;
                for (int i=0; i<outputBus->getNumberOfChannels(); ++i)
                {
                    const int outputChannelNum = outputBus->getChannelIndexInProcessBlockBuffer (i);
                    readFromDelayBuffer (buffer, i, outputChannelNum, mExpectedReadPosOne, delayLevelOne, endGain, false);
                }
            }
            
            // fade in at new position
            if (readPos != mExpectedReadPosOne)
            {
                for (int i=0; i<outputBus->getNumberOfChannels(); ++i)
                {
                    const int outputChannelNum = outputBus->getChannelIndexInProcessBlockBuffer (i);
                    readFromDelayBuffer (buffer, i, outputChannelNum, readPos, 0.0, delayLevelOne, false);
                }
            }
        }
        
        // add feedback to delay
        for (int i=0; i<inputBus->getNumberOfChannels(); ++i)
        {
            const int outputChannelNum = inputBus->getChannelIndexInProcessBlockBuffer (i);
            
            writeToDelayBuffer (mDelayBufferOne, buffer, outputChannelNum, i, mWritePosOne, mLastFeedbackGainOne, feedbackOne, false);
        }
        mLastFeedbackGainOne = feedbackOne;
        
        // advance positions
        mWritePosOne += buffer.getNumSamples();
        if (mWritePosOne >= mDelayBufferOne.getNumSamples())
            mWritePosOne -= mDelayBufferOne.getNumSamples();
        
        mExpectedReadPosOne = readPos + buffer.getNumSamples();
        if (mExpectedReadPosOne >= mDelayBufferOne.getNumSamples())
            mExpectedReadPosOne -= mDelayBufferOne.getNumSamples();
    }
}

void TapeDelayAudioProcessor::writeToDelayBuffer (AudioSampleBuffer& delayBuffer, AudioSampleBuffer& buffer,
                                                  const int channelIn, const int channelOut,
                                                  const int writePos, float startGain, float endGain, bool replacing)
{
    if (writePos + buffer.getNumSamples() <= mDelayBufferOne.getNumSamples())
    {
        if (replacing)
            delayBuffer.copyFromWithRamp (channelOut, writePos, buffer.getReadPointer (channelIn), buffer.getNumSamples(), startGain, endGain);
        else
            delayBuffer.addFromWithRamp (channelOut, writePos, buffer.getReadPointer (channelIn), buffer.getNumSamples(), startGain, endGain);
    }
    else
    {
        const auto midPos  = delayBuffer.getNumSamples() - writePos;
        const auto midGain = jmap (float (midPos) / buffer.getNumSamples(), startGain, endGain);
        if (replacing)
        {
            delayBuffer.copyFromWithRamp (channelOut, writePos, buffer.getReadPointer (channelIn),         midPos, startGain, midGain);
            delayBuffer.copyFromWithRamp (channelOut, 0,        buffer.getReadPointer (channelIn, midPos), buffer.getNumSamples() - midPos, midGain, endGain);
        }
        else
        {
            delayBuffer.addFromWithRamp (channelOut, writePos, buffer.getReadPointer (channelIn),         midPos, mLastInputGainOne, midGain);
            delayBuffer.addFromWithRamp (channelOut, 0,        buffer.getReadPointer (channelIn, midPos), buffer.getNumSamples() - midPos, midGain, endGain);
        }
    }
}

void TapeDelayAudioProcessor::readFromDelayBuffer (AudioSampleBuffer& buffer,
                                                   const int channelIn, const int channelOut,
                                                   const int readPos,
                                                   float startGain, float endGain,
                                                   bool replacing)
{
    
    if (readPos + buffer.getNumSamples() <= mDelayBufferOne.getNumSamples())
    {
        if (replacing)
            buffer.copyFromWithRamp (channelOut, 0, mDelayBufferOne.getReadPointer (channelIn, readPos), buffer.getNumSamples(), startGain, endGain);
        else
            buffer.addFromWithRamp (channelOut, 0, mDelayBufferOne.getReadPointer (channelIn, readPos), buffer.getNumSamples(), startGain, endGain);
    }
    else
    {
        const auto midPos  = mDelayBufferOne.getNumSamples() - readPos;
        const auto midGain = jmap (float (midPos) / buffer.getNumSamples(), startGain, endGain);
        if (replacing)
        {
            buffer.copyFromWithRamp (channelOut, 0,      mDelayBufferOne.getReadPointer (channelIn, readPos), midPos, startGain, midGain);
            buffer.copyFromWithRamp (channelOut, midPos, mDelayBufferOne.getReadPointer (channelIn), buffer.getNumSamples() - midPos, midGain, endGain);
        }
        else
        {
            buffer.addFromWithRamp (channelOut, 0,      mDelayBufferOne.getReadPointer (channelIn, readPos), midPos, startGain, midGain);
            buffer.addFromWithRamp (channelOut, midPos, mDelayBufferOne.getReadPointer (channelIn), buffer.getNumSamples() - midPos, midGain, endGain);
        }
    }
}

AudioProcessorValueTreeState& TapeDelayAudioProcessor::getValueTreeState()
{
    return mState;
}

//==============================================================================
bool TapeDelayAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* TapeDelayAudioProcessor::createEditor()
{
    return new TapeDelayAudioProcessorEditor (*this);
}

//==============================================================================
void TapeDelayAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    MemoryOutputStream stream(destData, false);
    mState.state.writeToStream (stream);
}

void TapeDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    ValueTree tree = ValueTree::readFromData (data, sizeInBytes);
    if (tree.isValid()) {
        mState.state = tree;
    }
}

//==============================================================================
const String TapeDelayAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TapeDelayAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool TapeDelayAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

double TapeDelayAudioProcessor::getTailLengthSeconds() const
{
    return 2.0;
}

int TapeDelayAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int TapeDelayAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TapeDelayAudioProcessor::setCurrentProgram (int index)
{
}

const String TapeDelayAudioProcessor::getProgramName (int index)
{
    return String();
}

void TapeDelayAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeDelayAudioProcessor();
}
