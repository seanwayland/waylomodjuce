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
    mDelayBufferTwo.setSize (getTotalNumOutputChannels(), 2.0 * (samplesPerBlock + sampleRate), false, false);
    mDelayBufferTwo.clear();
    
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
        const float delayLevelOne = 0.1;
        const float gain = Decibels::decibelsToGain (mGain.get());
        const float timeOne = mTimeOne.get();
        const float feedbackOne = Decibels::decibelsToGain (mFeedbackOne.get());
        
        const float delayLevelTwo = 0.1;
        
        const float timeTwo = 200;
        const float feedbackTwo = Decibels::decibelsToGain (mFeedbackOne.get());
        
        // write original to delay
        for (int i=0; i < mDelayBufferOne.getNumChannels(); ++i)
        {
            const int inputChannelNum = inputBus->getChannelIndexInProcessBlockBuffer (std::min (i, inputBus->getNumberOfChannels()));
            
            writeToDelayBuffer (mDelayBufferOne, buffer, inputChannelNum, i, mWritePosOne, 1.0f, 1.0f, true);
            writeToDelayBuffer (mDelayBufferTwo, buffer, inputChannelNum, i, mWritePosTwo, 1.0f, 1.0f, true);
        }
        
        // adapt dry gain
        buffer.applyGainRamp (0, buffer.getNumSamples(), mLastInputGainOne, gain);
        mLastInputGainOne = gain;
        buffer.applyGainRamp (0, buffer.getNumSamples(), mLastInputGainTwo, gain);
        mLastInputGainTwo = gain;
        
        // read delayed signal
        auto readPosOne = roundToInt (mWritePosOne - (mSampleRate * timeOne / 1000.0));
        if (readPosOne < 0)
            readPosOne += mDelayBufferOne.getNumSamples();
        
        auto readPosTwo = roundToInt (mWritePosOne - (mSampleRate * timeTwo / 1000.0));
        if (readPosTwo < 0)
            readPosTwo += mDelayBufferTwo.getNumSamples();
        
        if (Bus* outputBus = getBus (false, 0))
        {
            // if has run before
            if (mExpectedReadPosOne >= 0)
            {
                // fade out if readPos is off
                auto endGainOne = (readPosOne == mExpectedReadPosOne) ? delayLevelOne : 0.0f;
                for (int i=0; i<outputBus->getNumberOfChannels(); ++i)
                {
                    const int outputChannelNum = outputBus->getChannelIndexInProcessBlockBuffer (i);
                    readFromDelayBuffer (mDelayBufferOne, buffer, i, 0, mExpectedReadPosOne, delayLevelOne, endGainOne, false);
                }
            }
            
            if (mExpectedReadPosTwo >= 0)
            {
                // fade out if readPos is off
                auto endGainTwo = (readPosTwo == mExpectedReadPosTwo) ? delayLevelTwo : 0.0f;
                for (int i=0; i<outputBus->getNumberOfChannels(); ++i)
                {
                    const int outputChannelNum = outputBus->getChannelIndexInProcessBlockBuffer (i);
                    readFromDelayBuffer (mDelayBufferTwo, buffer, i, 1, mExpectedReadPosTwo, delayLevelTwo, endGainTwo, false);
                }
            }
            
            // fade in at new position
            if (readPosOne != mExpectedReadPosOne)
            {
                for (int i=0; i<outputBus->getNumberOfChannels(); ++i)
                {
                    const int outputChannelNum = outputBus->getChannelIndexInProcessBlockBuffer (i);
                    readFromDelayBuffer (mDelayBufferOne, buffer, i, 0, readPosOne, 0.0, delayLevelOne, false);
                }
            }
            
            
            // fade in at new position
            if (readPosTwo != mExpectedReadPosTwo)
            {
                for (int i=0; i<outputBus->getNumberOfChannels(); ++i)
                {
                    const int outputChannelNum = outputBus->getChannelIndexInProcessBlockBuffer (i);
                    readFromDelayBuffer (mDelayBufferTwo, buffer, i, 1, readPosTwo, 0.0, delayLevelTwo, false);
                }
            }
            
        }
        
        // add feedback to delay
        for (int i=0; i<inputBus->getNumberOfChannels(); ++i)
        {
            const int outputChannelNum = inputBus->getChannelIndexInProcessBlockBuffer (i);
            
            writeToDelayBuffer (mDelayBufferOne, buffer, outputChannelNum, 0, mWritePosOne, mLastFeedbackGainOne, feedbackOne, false);
        }
        
        for (int i=0; i<inputBus->getNumberOfChannels(); ++i)
        {
            const int outputChannelNum = inputBus->getChannelIndexInProcessBlockBuffer (i);
            
            writeToDelayBuffer (mDelayBufferTwo, buffer, outputChannelNum, 1, mWritePosTwo, mLastFeedbackGainTwo, feedbackTwo, false);
        }
        
        
        
        mLastFeedbackGainOne = feedbackOne;
        mLastFeedbackGainTwo = feedbackTwo;
        
        
        // advance positions
        mWritePosOne += buffer.getNumSamples();
        if (mWritePosOne >= mDelayBufferOne.getNumSamples())
            mWritePosOne -= mDelayBufferOne.getNumSamples();
        
        mWritePosTwo += buffer.getNumSamples();
        if (mWritePosTwo >= mDelayBufferTwo.getNumSamples())
            mWritePosTwo -= mDelayBufferTwo.getNumSamples();
        
        mExpectedReadPosOne = readPosOne + buffer.getNumSamples();
        if (mExpectedReadPosOne >= mDelayBufferOne.getNumSamples())
            mExpectedReadPosOne -= mDelayBufferOne.getNumSamples();
        
        mExpectedReadPosTwo = readPosTwo + buffer.getNumSamples();
        if (mExpectedReadPosTwo >= mDelayBufferTwo.getNumSamples())
            mExpectedReadPosTwo -= mDelayBufferTwo.getNumSamples();
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

void TapeDelayAudioProcessor::readFromDelayBuffer (AudioSampleBuffer& delayBuffer, AudioSampleBuffer& buffer,
                                                   const int channelIn, const int channelOut,
                                                   const int readPos,
                                                   float startGain, float endGain,
                                                   bool replacing)
{
    
    if (readPos + buffer.getNumSamples() <= delayBuffer.getNumSamples())
    {
        if (replacing)
            buffer.copyFromWithRamp (channelOut, 0, delayBuffer.getReadPointer (channelIn, readPos), buffer.getNumSamples(), startGain, endGain);
        else
            buffer.addFromWithRamp (channelOut, 0, delayBuffer.getReadPointer (channelIn, readPos), buffer.getNumSamples(), startGain, endGain);
    }
    else
    {
        const auto midPos  = delayBuffer.getNumSamples() - readPos;
        const auto midGain = jmap (float (midPos) / buffer.getNumSamples(), startGain, endGain);
        if (replacing)
        {
            buffer.copyFromWithRamp (channelOut, 0,      delayBuffer.getReadPointer (channelIn, readPos), midPos, startGain, midGain);
            buffer.copyFromWithRamp (channelOut, midPos, delayBuffer.getReadPointer (channelIn), buffer.getNumSamples() - midPos, midGain, endGain);
        }
        else
        {
            buffer.addFromWithRamp (channelOut, 0, delayBuffer.getReadPointer (channelIn, readPos), midPos, startGain, midGain);
            buffer.addFromWithRamp (channelOut, midPos, delayBuffer.getReadPointer (channelIn), buffer.getNumSamples() - midPos, midGain, endGain);
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
