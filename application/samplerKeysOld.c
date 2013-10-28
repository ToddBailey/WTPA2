// Old sampler key controls.  Happened in substate 1 of DoSampler

		if(editModeEntered==false)	// Normal functions for buttons?
		{
			if(keyState&Im_EFFECT)			// If we're holding the effect switch, our other switches call up patches instead of their normal functions.  It's like a shift key.
			{
				// Multiple Held-key combinations:
				if(((keyState&Im_SWITCH_3)&&(newKeys&Im_SWITCH_4))||((newKeys&Im_SWITCH_3)&&(keyState&Im_SWITCH_4)))	// Bail!
				{
					UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
					RevertSampleToUnadjusted(currentBank);			// Get rid of any trimming on the sample.
					bankStates[currentBank].bitReduction=0;			// No crusties yet.
					bankStates[currentBank].jitterValue=0;			// No hissies yet.
					bankStates[currentBank].granularSlices=0;		// No remix yet.
					bankStates[currentBank].halfSpeed=false;
					bankStates[currentBank].backwardsPlayback=false;
					bankStates[currentBank].sampleDirection=true;
					bankStates[currentBank].loopOnce=false;
					editModeEntered=false;
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_CANCEL_EFFECTS,0);		// Send it out to the techno nerds.
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REVERT_SAMPLE_TO_FULL,0);		// Send it out to the techno nerds.
				}
				else if(keyState&Im_SWITCH_3)	// Sample trimming
				{
					if(keyState&Im_SWITCH_0||keyState&Im_SWITCH_1||keyState&Im_SWITCH_2)	// These are all edit commands, if we hit them then enter edit mode.
					{
						editModeEntered=true;
					}
					else if(newKeys&Im_SWITCH_3)		// Screw and chop (toggle) (default two key combo)
					{
						if(bankStates[currentBank].halfSpeed==false)
						{
							bankStates[currentBank].halfSpeed=true;
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_HALF_SPEED,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
						}
						else
						{
							bankStates[currentBank].halfSpeed=false;
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_HALF_SPEED,0);		// Send it out to the techno nerds.
						}
					}
				}
				else if(keyState&Im_SWITCH_4)		// Realtime.
				{
					if(newKeys&Im_SWITCH_2)		// Do realtime (three button combo)
					{
						StartRealtime(currentBank,CLK_EXTERNAL,0);
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REALTIME,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
					}
					else if(newKeys&Im_SWITCH_4)		// "Paul is Dead" mask (only pressing two keys)
					{
						if(bankStates[currentBank].backwardsPlayback==false)
						{
							bankStates[currentBank].backwardsPlayback=true;
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_PLAY_BACKWARDS,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
						}
						else
						{
							bankStates[currentBank].backwardsPlayback=false;
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_PLAY_BACKWARDS,0);		// Send it out to the techno nerds.
						}

						UpdateAdjustedSampleAddresses(currentBank);	// @@@ make sure we handle going backwards when considering edited samples.
					}
				}
				else
				{
					if(newKeys&Im_SWITCH_0)		// Switch 0 (the left most) handles bit reduction.
					{
						bankStates[currentBank].bitReduction=scaledEncoderValue;	// Reduce bit depth by 0-7.
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_BIT_REDUCTION,scaledEncoderValue);		// Send it out to the techno nerds.
					}
					if(newKeys&Im_SWITCH_1)		// Switch 1 sends granular data.
					{
						MakeNewGranularArray(currentBank,(encoderValue/2));			// Start or stop granularization.
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_GRANULARITY,(encoderValue/2));		// Send it out to the techno nerds.
					}
					if(newKeys&Im_SWITCH_2)		// Switch 2 assigns our different ways of combining audio channels on the output.
					{
						switch(scaledEncoderValue)
						{
							case 0:
							UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;

							case 1:
							UpdateOutput=OutputMultiplyBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;

							case 2:
							UpdateOutput=OutputAndBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;

							case 3:
							UpdateOutput=OutputXorBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;

							default:
							break;
						}
					}
				}
			}
			else
			{
				if(newKeys&Im_REC)										// Record switch pressed.
				{
					if(bankStates[currentBank].audioFunction==AUDIO_RECORD)			// Were we recording already?
					{
						StartPlayback(currentBank,CLK_EXTERNAL,0);					// Begin playing back the loop we just recorded (ext clock)
						bankStates[currentBank].loopOnce=false;
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
					}
					else											// We're not recording right now, so start doing it.
					{
						StartRecording(currentBank,CLK_EXTERNAL,0);	// Start recording (ext clock)
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_RECORDING,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
					}
				}

				else if(newKeys&Im_ODUB)			// Overdub switch pressed.  Odub is similar to record except it takes its recording from a different analog input, and it cannot be invoked unless there is already a sample in the bank.
				{
					if(bankStates[currentBank].audioFunction==AUDIO_OVERDUB)		// Were we overdubbing already?
					{
						ContinuePlayback(currentBank,CLK_EXTERNAL,0);				// Begin playing back the loop we just recorded (ext clock, not from the beginning)
						bankStates[currentBank].loopOnce=false;
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OVERDUB,0);		// Send it out to the techno nerds.
					}
					else							// We're not recording right now, so start doing it.
					{
						if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)		// Because of how OVERDUB thinks about memory we can't do it unless you there's already a sample in the bank.
						{
							StartOverdub(currentBank,CLK_EXTERNAL,0);					// Get bizzy (ext clock).
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OVERDUB,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
						}
					}
				}
				else if(newKeys&Im_PLAY_PAUSE)		// Play / Pause switch pressed.  If anything is happening this will stop it.  Otherwise, this will start playing back AS A LOOP.  This will not restart a playing sample from the beginning.
				{
					if(bankStates[currentBank].audioFunction==AUDIO_IDLE)		// Doing nothing?
					{
						if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
						{
							ContinuePlayback(currentBank,CLK_EXTERNAL,0);			// Continue playing back from wherever we are in the sample memory (ext clock, not from the beginning) @@@ So as of now, this will begin playback at the END of a sample if we've just finished recording.  Ugly.
							bankStates[currentBank].loopOnce=false;
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
						}
					}
					else		// Pause whatever we were doing.
					{
						bankStates[currentBank].audioFunction=AUDIO_IDLE;		// Nothing to do in the ISR
						bankStates[currentBank].clockMode=CLK_NONE;				// Don't trigger this bank.
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_OFF,MIDI_GENERIC_NOTE,0);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
					}

				}
//					else if(newKeys&Im_SINGLE_PLAY)		// Stop whatever we're doing and play the sample from the beginning, one time.
//					{
//						if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
//						{
//							StartPlayback(currentBank,CLK_EXTERNAL,0);			// Play back this sample.
//							bankStates[currentBank].loopOnce=true;				// And do it one time, for your mind.
//							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
//						}
//					}
				else if(newKeys&Im_BANK)		// Increment through banks when this button is pressed.
				{
					currentBank++;
					if(currentBank>=NUM_BANKS)
					{
						currentBank=BANK_0;		// Loop around.
					}
				}
// ------------------------------------
// FLASH TEST
				else if(newKeys&Im_SWITCH_6)	// Write sample to slot 0 from bank 0
				{
					WriteSampleToSd(currentBank,0);
				}
				else if(newKeys&Im_SWITCH_7)	// Read sample from slot 0 to bank 0
				{
					ReadSampleFromSd(currentBank,0);
				}
				else if(newKeys&Im_SINGLE_PLAY)		// @@@TEST Play direct from the SD
				{
					PlaySampleFromSd(BANK_0,0);
				}
// ------------------------------------
			}
		}
		else	// In edit mode.
		{
			if(keyState&Im_SWITCH_0)		// Adjust start (three button combo)
			{
				if(bankStates[currentBank].sampleStartOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
				{
					AdjustSampleStart(currentBank,encoderValue);
					i=encoderValue/2;		// Make into MIDI-worthy value (this will line up with the coarse adjust messages)
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_START_WIDE,i);		// Send it out to the techno nerds.
				}
			}
			else if(keyState&Im_SWITCH_1)		// Adjust end (three button combo)
			{
				if(bankStates[currentBank].sampleEndOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
				{
					AdjustSampleEnd(currentBank,encoderValue);
					i=encoderValue/2;		// Make into MIDI-worthy value (this will line up with the coarse adjust messages)
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_END_WIDE,i);		// Send it out to the techno nerds.
				}
			}
			else if(keyState&Im_SWITCH_2)		// Adjust window (three button combo)
			{
				if(bankStates[currentBank].sampleWindowOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
				{
					AdjustSampleWindow(currentBank,encoderValue);
					i=encoderValue/2;		// Make into MIDI-worthy value (this will line up with the coarse adjust messages)
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_WINDOW_WIDE,i);		// Send it out to the techno nerds.
				}
			}
			else if(newKeys&Im_SWITCH_3||newKeys&Im_SWITCH_4||newKeys&Im_SWITCH_5)	// Non edit-mode key hit, bail from edit mode.
			{
				editModeEntered=false;
			}
		}
