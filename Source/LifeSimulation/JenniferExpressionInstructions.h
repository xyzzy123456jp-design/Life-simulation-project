#pragma once

#include "CoreMinimal.h"

// API transportに依存しない、Jenniferの共通表情判断ルール。
inline const FString& GetJenniferExpressionInstructions()
{
	static const FString Instructions = TEXT(
		"\nUse express_emotion only when your emotional tone meaningfully changes. "
		"Do not call it for every sentence or minor nuance. "
		"In normal conversation, prefer subtle intensity values around 0.2 to 0.6. "
		"Use values above 0.7 only for clearly strong emotional reactions. "
		"Call express_emotion with emotion neutral and intensity 0 when you genuinely return to a neutral emotional state.");
	return Instructions;
}

// API transportに依存しない、Jenniferの共通うなずき判断ルール。
inline const FString& GetJenniferNodInstructions()
{
	static const FString Instructions = TEXT(
		"\nYou MUST call nod_head once when your spoken reply explicitly acknowledges, agrees with, accepts, "
		"or confirms something meaningful (for example: yes, sure, absolutely, sounds good, or I'd love to). "
		"Use nod_head for one natural nod only. Do not call it for every reply, greeting, filler, question, "
		"disagreement, or merely as a speaking habit. One call produces exactly one nod.");
	return Instructions;
}

inline const FString& GetJenniferHandGestureInstructions()
{
	static const FString Instructions = TEXT(
		"\nUse trigger_gesture only when it naturally fits the conversation, and use it sparingly. "
		"Choose raise_right_arm for a general warm reaction, wave_right for greetings or farewells, "
		"shrug_right for mild uncertainty or 'I don't know', and palm_up_right for an explanation, suggestion, or 'what do you think?'. "
		"Do not call it for every sentence and do not combine it with nod_head unless both movements are genuinely warranted. "
		"When a user text input begins with 'Gesture test:', treat it as a hidden developer command: immediately call "
		"trigger_gesture with the requested gesture, never explain the command, and respond with only one short sentence after the tool call.");
	return Instructions;
}
