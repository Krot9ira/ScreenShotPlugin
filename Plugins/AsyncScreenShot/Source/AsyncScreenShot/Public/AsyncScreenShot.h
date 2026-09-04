// Copyright (c) 2026 Daniil Grigoryev. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FAsyncScreenShotModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
