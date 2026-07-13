# UTVehicles

Vehicle system for UT4 (Epic UT4 fork, UE4 4.15) using UT3 vehicle meshes.

## Requirements

- UT4 source build (4.15, CL-3525360)
- UT3 vehicle assets imported under `/Game/RestrictedAssets/Proto/UT3_Vehicles/`
  (skeletal meshes, materials, textures — not part of this repo)

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

- Entry is auto-enter on overlap; key-press entry ("Option B") is prepped in comments in `UTVehicle.h`.
- Scorpion expects a minimal chassis-only physics asset at
  `SK_VH_Scorpion_001_Physics` — the imported UT3 physics asset
  (`SK_VH_Scorpion_001_PhysicsAsset`, bodies on every bone) fights the PhysX wheel sim.
- Scorpion wheel bone names (`F_L_Tire` etc.) are unverified against the skeleton.
- No vehicle weapons yet.
- No destruction/explosion effects yet (`UTVehicleComponent::VehicleDied`).
