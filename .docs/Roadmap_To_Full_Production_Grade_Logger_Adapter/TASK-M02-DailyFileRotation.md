# M02 — Daily Time-Based Rotation

- **Priority**: 🟡 Medium
- **Est. Effort**: 1 hour
- **Depends on**: None

---

## Problem

Current rotation is size-based only (`RotatingFileSinkConfig::set_rotation_max_file_size`). There is no time-based rotation (daily/hourly).

For a trading system, daily logs are standard:
```
logs/assembler-2026-06-11.log
logs/assembler-2026-06-12.log
```

---

## Implementation

**Note**: Quill v10.0.1 does not include a dedicated `DailyFileSink`. Two approaches:

### Approach A — Use `RotatingFileSinkConfig::set_rotation_time_based()` (if available)

```cpp
RotatingFileSinkConfig rotating_cfg;
rotating_cfg.set_rotation_max_file_size(config.file.max_file_size);
rotating_cfg.set_max_backup_files(config.file.max_files);
// Time-based rotation every 24h
rotating_cfg.set_rotation_time_based("d", 1); // "d" = daily, 1 = every day
```

If `set_rotation_time_based` is not available in v10.0.1, fall back to Approach B.

### Approach B — Implement rotation wrapper in Logger_Adapter

Use `std::filesystem::last_write_time()` at log time — if the file's last write time is from a previous day, rename the file and create a new sink:

```cpp
// Log internally checks if file needs rotation based on date
// This is a heavier approach; only if Quill doesn't support it
```

---

## Acceptance Criteria

- [ ] After midnight, new log messages go to a new dated file
- [ ] Old log files are not deleted (unless `max_files` limits them)
- [ ] Build succeeds Debug|x64
