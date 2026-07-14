# UTVehicles

Vehicle system for UT4 (Epic UT4 fork, UE4 4.15) using UT3 vehicle meshes.

## Requirements

- UT4 source build (4.15, CL-3525360)
- UT3 vehicle assets imported under `/Game/RestrictedAssets/Proto/UT3_Vehicles/`
  (skeletal meshes, materials, textures — not part of this repo)
- Working vehicle-pack assets imported under `/Game/Mogno/Vehicles/`
  (Scorpion tire/collision assets and temporary Goliath engine sounds)

## Layout

| Class | Purpose |
|-------|---------|
| `AUTVehicle` | Wheeled vehicle base (PhysX `AWheeledVehicle`), camera boom, team interface, enter/exit RPCs |
| `UUTVehicleComponent` | Shared logic: driver, health, damage, team, entry trigger, respawn, HUD |
| `AUTVehicleFlying` + `UUTVehicleMovementHover` | Flying vehicle base with custom hover/flight movement |
| `AUTVehicle_Scorpion` | Wheeled — full 4W setup (wheels, gears, differential, steering curve) |
| `AUTVehicle_Raptor` | Flying — tuned hover movement |
| `AUTVehicleSpawner` | Map-placeable, team-restricted vehicle spawn pad with respawn |

## Status / known gaps

- Nearby empty vehicles show the player's current `ActivateSpecial` key; the
  same stock, remappable action enters and exits the vehicle.
- Imported UT3 physics assets (bodies on every bone) are used as-is: wheeled
  vehicles disable every body except the chassis in
  `AUTVehicle::PostInitializeComponents`, so no chassis-only asset is needed.
  Flying vehicles move kinematically, so their bodies never simulate.
- The Scorpion uses a native code-only wheel pose for steering, suspension and
  tire rotation on its verified `F_L_Tire`, `F_R_Tire`, `B_L_Tire` and
  `B_R_Tire` bones.
- Empty-vehicle RPC ownership currently selects one overlapping controller;
  simultaneous multiplayer entry still needs contention testing.
- No vehicle weapons yet.
- No destruction/explosion effects yet (`UTVehicleComponent::VehicleDied`).
