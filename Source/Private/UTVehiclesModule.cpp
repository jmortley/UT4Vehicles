#include "UnrealTournament.h"
#include "Modules/ModuleManager.h"

class FUTVehiclesModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FUTVehiclesModule, UTVehicles)
