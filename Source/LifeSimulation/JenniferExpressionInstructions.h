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
