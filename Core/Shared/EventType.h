#pragma once

enum class EventType
{
	Nmi,
	Irq,
	StartFrame,
	EndFrame,
	Reset,
	InputPolled,
	StateLoaded,
	StateSaved,
	CodeBreak,

	LastValue
};