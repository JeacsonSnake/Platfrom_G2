# Plan: Multi-Device Motor Status Board & Task Registration

## Date
2026-07-16

## Scope
Modify the `Spinning.vue` dashboard so that the **Motor Status Board** and **Register Spin Task** panels support multiple ESP32S3 devices distinguished by MAC-format device IDs.

## Requirements Confirmed with User
1. **Each Device has its own 4 motors (0-3).** The global `Motor` table remains the 4-motor template; live telemetry is fetched per `device_id`.
2. **Register Spin Task** must explicitly select a **Device** first, then allow multi-select of that device's motors, and dispatch to the selected device.
3. **Manual refresh** button coexists with the existing 5-second auto-polling. Clicking refresh immediately fetches the currently selected device's motor status.
4. **Multi-motor scheduling** stores a **single `Spinning` record with a motor list**; the scheduler dispatches commands to each selected motor.
5. **Device tabs** in Motor Status Board are labeled by **MAC address** (e.g. `7c:df:a1:e6:d3:cc`).

## Key Findings from Codebase
- `vue_frontend/src/views/Dashboard/Spinning.vue` currently polls `/api/get_motors/` and `/api/spinning/` every 5s and passes `motors` to `MotorStatusBoard` and `ScheduleForm`.
- `MotorStatusBoard.vue` renders a plain 6-column table (`ID`, `Name`, `Availability`, `Status`, `Target RPM`, `Actual RPM`).
- `ScheduleForm.vue` has a single motor dropdown, datetime, speed, and duration.
- Backend `get_motors` (`django_backend/main_page/views.py`) auto-resolves a single dispatchable device via `resolve_dispatchable_device_id()` and returns only that device's 4 motors.
- Backend `spinning` view auto-fills `device_id = resolve_dispatchable_device_id()`; frontend cannot choose the target device.
- `Spinning` model has a single `motor_name` field and a `device_id` field.
- `SpinningScheduler` (`scheduler.py`) dispatches one motor per record.
- Device registry and live state already exist (`Device` model, `/api/device_list/`, `_device_states` in `mqtt.py`).
- Git root: `E:/Platform_G2`. Current uncommitted change: `django_backend/db.sqlite3` (must be excluded from commit).

## Implementation Approach

### Backend Changes

#### 1. `django_backend/main_page/models.py`
- Add a JSONField `motor_names` to `Spinning` to store the list of selected motors for multi-motor tasks.
- Keep `motor_name` for backward compatibility with existing records (legacy single motor).
- Add a helper property `effective_motor_names()` that returns `motor_names` if present, otherwise `[motor_name]`.

#### 2. `django_backend/main_page/views.py`
- Modify `get_motors(request)`:
  - Accept `request.data.get('device_id')`.
  - If provided and the device exists in `_device_states` or `Device` registry, use it as `target_device_id`.
  - Otherwise fall back to `resolve_dispatchable_device_id()` for backward compatibility.
  - Return an extra field `device_id` in the response so the frontend knows which device the data belongs to.
- Modify `spinning(request)`:
  - Accept `device_id` from `request.data['data']`.
  - Accept `motor_names` list from payload.
  - Validate that the device exists; if not, fall back to `resolve_dispatchable_device_id()`.
  - Validate that all `motor_names` exist in the `Motor` table.
  - Store `device_id` and `motor_names` in the `Spinning` record.
  - For backward compatibility, also set `motor_name = motor_names[0]`.
- Add a small utility `format_mac(device_id_or_mac)` used to render MAC labels consistently.

#### 3. `django_backend/main_page/scheduler.py`
- Modify `_dispatch_task(task)`:
  - Use `task.effective_motor_names()`.
  - Resolve device once via `resolve_dispatchable_device_id(task.device_id)`.
  - Loop over motors; for each, call `dispatch_motor_task(device_id, motor.motor_index, task.motor_speed, task.duration_sec)`.
  - Mark record `SENT` only if all succeed; mark `FAILED` if any dispatch fails (record the first error).

#### 4. `django_backend/main_page/serializer.py`
- Update `SpinningSerializer` so that `motor_names` is writable and `motor_name` remains optional for new records.

#### 5. Database Migration
- Create an auto-migration for the new `Spinning.motor_names` JSONField:
  ```bash
  cd /e/Platform_G2/django_backend
  python manage.py makemigrations main_page
  ```

### Frontend Changes

#### 1. `vue_frontend/src/services/api/motors.js`
- Change `getList(token)` to `getList(token, deviceId = null)` and include `device_id` in the POST body when provided.
- Change `createSchedule(token, payload)` payload shape to include `device_id` and `motor_names` instead of only `motor_name`.
- Add `getDeviceMotors(token, deviceId)` convenience wrapper if desired.

#### 2. `vue_frontend/src/services/api/devices.js`
- Ensure `getList()` already calls `/api/device_list/` (it does). No changes needed unless token auth is required; current endpoint is `GET` and unauthenticated. If the project requires auth, add token header. Based on current code, keep it as-is.

#### 3. `vue_frontend/src/components/spinning/MotorStatusBoard.vue`
- Add props: `devices` (Array), `selectedDeviceId` (String), `loading` (Boolean).
- Render an Excel-like tab bar at the bottom (or top) of the panel:
  - Each tab label = formatted MAC address of the device.
  - Active tab highlighted.
  - Clicking a tab emits `select-device`.
- Add a **Manual Refresh** button in the panel header area; clicking emits `refresh`.
- Keep the existing 6-column table but render it for the currently selected device only.
- Show device-level summary above the table (online/offline, task status).

#### 4. `vue_frontend/src/components/spinning/ScheduleForm.vue`
- Add props: `devices` (Array), `motors` (Array — still used as motor template).
- Change `modelValue` structure to:
  ```js
  {
    device_id: '',
    motor_names: [], // multi-select
    scheduled_time: '',
    motor_speed: 0,
    duration_sec: 0
  }
  ```
- Add a **Device** dropdown; when changed, reset `motor_names`.
- Replace single **Motor Selection** dropdown with a multi-select checklist (e.g. `select multiple` or Element Plus `el-select` with `multiple`). Options are the global 4 motors; label can include device context.
- Keep speed, duration, scheduled time, and immediate button.
- Emit `submit` on button click.

#### 5. `vue_frontend/src/components/spinning/ScheduleQueue.vue`
- Update the table to display `Device` (MAC/label) and `Motors` (joined list) columns.
- Backward compatibility: if a legacy record has only `motor_name`, display that.

#### 6. `vue_frontend/src/views/Dashboard/Spinning.vue`
- Add local state:
  - `devices: []`
  - `selectedDeviceId: ''`
  - `deviceMotors: {}` — cache of `{ [deviceId]: motor_list }`
  - `loadingStatus: false`
- Fetch device list on mount via `devicesApi.getList()`.
- After device list loads, default `selectedDeviceId` to the first online device or first device.
- Modify `getMotors()` to call `motorsApi.getList(token, selectedDeviceId)` and store result in `deviceMotors[selectedDeviceId]`.
- Pass `filteredMotors = deviceMotors[selectedDeviceId] || []` to `MotorStatusBoard` and `ScheduleForm`.
- Keep 5s polling for records and selected-device motors.
- Handle `select-device` event from `MotorStatusBoard` to switch `selectedDeviceId` and immediately fetch motors.
- Handle `refresh` event to immediately fetch selected device motors.
- When submitting schedule, pass `device_id` and `motor_names` in payload.
- Update `ConsoleHeader` status items to show device count and motor count.

## File Changes Summary

### Backend
- `django_backend/main_page/models.py` — add `motor_names` JSONField and helper property.
- `django_backend/main_page/serializer.py` — expose `motor_names`.
- `django_backend/main_page/views.py` — `get_motors` accepts `device_id`; `spinning` accepts `device_id` + `motor_names`.
- `django_backend/main_page/scheduler.py` — dispatch to multiple motors in one record.
- New migration file in `django_backend/main_page/migrations/` for `Spinning.motor_names`.

### Frontend
- `vue_frontend/src/services/api/motors.js` — support `device_id` and new payload shape.
- `vue_frontend/src/components/spinning/MotorStatusBoard.vue` — device tabs + manual refresh.
- `vue_frontend/src/components/spinning/ScheduleForm.vue` — device + multi-motor selection.
- `vue_frontend/src/components/spinning/ScheduleQueue.vue` — show device and motors list.
- `vue_frontend/src/views/Dashboard/Spinning.vue` — orchestrate device selection and per-device data.
- `vue_frontend/src/__tests__/api/motors.spec.js` — update tests for new `getList` and `createSchedule` signatures.

## Testing Plan
1. Run backend migration and start Django.
2. Create/register at least two `Device` records with different MAC addresses.
3. Open `Spinning.vue`:
   - Verify device tabs appear with MAC labels.
   - Switch tabs; verify `get_motors` request includes `device_id`.
   - Click **Manual Refresh**; verify immediate request and table update.
4. Register a multi-motor task:
   - Select a device, select Motors 0 and 2, set speed/duration, submit.
   - Verify `/api/spinning/` payload contains `device_id` and `motor_names: ['Motor 0', 'Motor 2']`.
   - Verify `SpinningScheduler` dispatches two commands.
5. Verify legacy records with single `motor_name` still display and dispatch correctly.
6. Run frontend unit tests: `cd vue_frontend && npm test` (or vitest) and update assertions.

## Git Commit Instructions
- Commit from `E:/Platform_G2`.
- **Exclude** `django_backend/db.sqlite3`.
- Suggested commit message:
  ```
  feat(spinning): multi-device motor status board and task registration

  - Motor Status Board now groups motors by Device with MAC-labeled tabs
  - Add manual refresh button to fetch selected device's live motor status
  - Register Spin Task supports explicit Device selection and multi-motor dispatch
  - Backend get_motors and spinning endpoints accept device_id and motor_names
  - SpinningScheduler dispatches commands to each motor in a single record
  ```

## Rollback Notes
- Migration adds a nullable JSONField with default `list`; safe to reverse.
- Legacy `motor_name` is kept populated, so old records continue to work even if `motor_names` is empty.
