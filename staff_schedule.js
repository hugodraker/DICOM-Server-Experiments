// ============================================================================
//   RIS Staff Schedule Adapter -> SQLite (staff_shift_schedules)
// ============================================================================
let g_facilities = [];
let g_rooms = [];

async function execSQL(query) {
    const res = await fetch('/api/query', { method: 'POST', body: query });
    return await res.json();
}

function toSqlDateTime(dateSlash, totalMins) {
    const [y, m, d] = dateSlash.split('/').map(Number);
    const dt = new Date(y, m - 1, d, 0, 0, 0);
    dt.setMinutes(totalMins);
    const pad = n => String(n).padStart(2, '0');
    return `${dt.getFullYear()}-${pad(dt.getMonth() + 1)}-${pad(dt.getDate())} ${pad(dt.getHours())}:${pad(dt.getMinutes())}:00`;
}

function fromSqlRange(startStr, endStr) {
    const s = new Date(startStr.replace(' ', 'T'));
    const e = new Date(endStr.replace(' ', 'T'));
    const pad = n => String(n).padStart(2, '0');
    const date = `${s.getFullYear()}/${pad(s.getMonth() + 1)}/${pad(s.getDate())}`;
    const startMin = s.getHours() * 60 + s.getMinutes();
    let duration = Math.round((e - s) / 60000);
    if (isNaN(duration) || duration < 15) duration = 60;
    if (duration > 1440 - startMin) duration = Math.max(15, 1440 - startMin);
    return { date, startMin, duration };
}

function formatShiftTitle(shiftType, roomId) {
    const room = g_rooms.find(r => String(r.modality_id) === String(roomId));
    return room ? `${shiftType} [${room.room_name}]` : shiftType;
}

const StaffScheduleAdapter = {
    loadSetting(key, def) {
        const v = localStorage.getItem('ris_sched_' + key);
        return v !== null ? v : def;
    },
    saveSetting(key, val) {
        localStorage.setItem('ris_sched_' + key, val);
    },

    async loadGlobalSettings() {
        const rows = await execSQL(`SELECT setting_key, setting_value FROM app_settings`);
        const map = {};
        if (Array.isArray(rows)) rows.forEach(r => map[r.setting_key] = r.setting_value);
        return map;
    },

    async saveGlobalSetting(key, val) {
        const safeVal = String(val || '').replace(/'/g, "''");
        await execSQL(`INSERT OR REPLACE INTO app_settings (setting_key, setting_value) VALUES ('${key}', '${safeVal}')`);
    },

    async saveUserProfile(userId, prof) {
        const esc = v => (v === null || v === undefined) ? '' : String(v).replace(/'/g, "''");
        const numOrNull = v => (v === '' || v === null || v === undefined || isNaN(parseFloat(v))) ? 'NULL' : parseFloat(v);
        const isSched = prof.is_schedulable === false ? 0 : 1;
        await execSQL(`
            INSERT OR REPLACE INTO staff_schedule_profiles
            (user_id, is_schedulable, coverage_hours, preferred_hours, allowed_days, rest_hours, max_daily_hours, max_weekly_hours)
            VALUES (${userId}, ${isSched}, '${esc(prof.c)}', '${esc(prof.p)}', '${esc(prof.d)}', ${numOrNull(prof.r)}, ${numOrNull(prof.da)}, ${numOrNull(prof.w)})
        `);
    },

    async loadData() {
        const [users, facs, rooms, shifts] = await Promise.all([
            execSQL(`
                SELECT u.user_id, u.first_name, u.last_name, u.role_id,
                       COALESCE(sp.is_schedulable, 1) AS is_schedulable,
                       sp.default_facility_id, sp.default_modality_id,
                       COALESCE(sp.coverage_hours, '') AS coverage_hours,
                       COALESCE(sp.preferred_hours, '') AS preferred_hours,
                       COALESCE(sp.allowed_days, '') AS allowed_days,
                       sp.rest_hours, sp.max_daily_hours, sp.max_weekly_hours
                FROM staff_users u
                LEFT JOIN staff_schedule_profiles sp ON u.user_id = sp.user_id
                WHERE u.is_active = 1 AND u.role_id != 6
                ORDER BY u.last_name, u.first_name
            `),
            execSQL(`SELECT facility_id, facility_name FROM facilities WHERE is_active = 1 ORDER BY facility_name`),
            execSQL(`SELECT modality_id, room_name, modality_type FROM modalities_and_rooms WHERE operational_status = 'ONLINE' ORDER BY room_name`),
            execSQL(`SELECT shift_id, user_id, facility_id, assigned_modality_id, shift_type, shift_start, shift_end, notes FROM staff_shift_schedules ORDER BY shift_start`)
        ]);

        g_facilities = Array.isArray(facs) ? facs : [];
        g_rooms = Array.isArray(rooms) ? rooms : [];

        const facOpts = g_facilities.map(f => `<option value="${f.facility_id}">${f.facility_name}</option>`).join('');
        const roomOpts = `<option value="">-- No Room --</option>` + g_rooms.map(r => `<option value="${r.modality_id}">${r.room_name} [${r.modality_type}]</option>`).join('');
        ['comboDefaultFacility', 'editShiftFacility'].forEach(id => { const el = document.getElementById(id); if (el && !el.options.length) el.innerHTML = facOpts; });
        ['comboDefaultRoom', 'editShiftRoom'].forEach(id => { const el = document.getElementById(id); if (el && el.options.length <= 1) el.innerHTML = roomOpts; });

        const people = (Array.isArray(users) ? users : []).map(u => ({
            id: u.user_id,
            name: `${u.first_name} ${u.last_name}`,
            meta: u,
            profile: {
                is_schedulable: u.is_schedulable !== 0,
                c: u.coverage_hours || '',
                p: u.preferred_hours || '',
                d: u.allowed_days || '',
                r: u.rest_hours !== null && u.rest_hours !== undefined ? String(u.rest_hours) : '',
                da: u.max_daily_hours !== null && u.max_daily_hours !== undefined ? String(u.max_daily_hours) : '',
                w: u.max_weekly_hours !== null && u.max_weekly_hours !== undefined ? String(u.max_weekly_hours) : ''
            }
        }));

        const userIdxMap = new Map(people.map((p, idx) => [p.id, idx]));
        const colors = ["#039BE5", "#33B679", "#8E24AA", "#F4511E", "#E67C73", "#00897B", "#3949AB", "#6D4C41"];

        const events = [];
        (Array.isArray(shifts) ? shifts : []).forEach(s => {
            const pIdx = userIdxMap.get(s.user_id);
            if (pIdx === undefined) return;
            const { date, startMin, duration } = fromSqlRange(s.shift_start, s.shift_end);
            const isOff = (s.shift_type || '').startsWith('OFF');
            events.push({
                id: String(s.shift_id),
                personIdx: pIdx,
                date,
                startMin,
                duration,
                title: formatShiftTitle(s.shift_type, s.assigned_modality_id),
                color: isOff ? "#D93025" : colors[pIdx % colors.length],
                meta: {
                    facility_id: s.facility_id,
                    assigned_modality_id: s.assigned_modality_id,
                    shift_type: s.shift_type,
                    notes: s.notes || ''
                }
            });
        });

        return { people, events };
    },

    createDefaultEvent(personIdx, personObj, dateStr, startMin, duration, color) {
        const shiftType = document.getElementById('comboDefaultEvent')?.value || 'WORKING_REGULAR';
        const facId = document.getElementById('comboDefaultFacility')?.value || (g_facilities[0]?.facility_id || 1);
        const roomId = document.getElementById('comboDefaultRoom')?.value || null;
        return {
            id: null,
            personIdx,
            date: dateStr,
            startMin,
            duration,
            title: formatShiftTitle(shiftType, roomId),
            color: shiftType.startsWith('OFF') ? "#D93025" : color,
            meta: {
                facility_id: parseInt(facId, 10),
                assigned_modality_id: roomId ? parseInt(roomId, 10) : null,
                shift_type: shiftType,
                notes: ''
            }
        };
    },

    async saveEvent(ev, personObj) {
        const people = CalendarEngine.getPeople();
        const user = personObj || people[ev.personIdx];
        if (!user) return null;

        const startSql = toSqlDateTime(ev.date, ev.startMin);
        const endSql = toSqlDateTime(ev.date, ev.startMin + ev.duration);
        const facId = ev.meta?.facility_id || (g_facilities[0]?.facility_id || 1);
        const roomVal = ev.meta?.assigned_modality_id ? ev.meta.assigned_modality_id : 'NULL';
        const shiftType = ev.meta?.shift_type || 'WORKING_REGULAR';
        const notes = (ev.meta?.notes || '').replace(/'/g, "''");

        if (ev.id) {
            const res = await execSQL(`
                UPDATE staff_shift_schedules
                SET user_id = ${user.id}, facility_id = ${facId}, assigned_modality_id = ${roomVal},
                    shift_type = '${shiftType}', shift_start = '${startSql}', shift_end = '${endSql}', notes = '${notes}'
                WHERE shift_id = ${ev.id}
            `);
            if (res && res.error) { alert("SQL Error: " + res.error); return null; }
            ev.title = formatShiftTitle(shiftType, ev.meta?.assigned_modality_id);
            return ev;
        } else {
            const res = await execSQL(`
                INSERT INTO staff_shift_schedules (user_id, facility_id, assigned_modality_id, shift_type, shift_start, shift_end, notes)
                VALUES (${user.id}, ${facId}, ${roomVal}, '${shiftType}', '${startSql}', '${endSql}', '${notes}')
            `);
            if (res && res.error) { alert("SQL Error: " + res.error); return null; }
            const idRes = await execSQL(`SELECT MAX(shift_id) AS id FROM staff_shift_schedules WHERE user_id = ${user.id} AND shift_start = '${startSql}'`);
            ev.id = (Array.isArray(idRes) && idRes[0]?.id) ? String(idRes[0].id) : String(Date.now());
            ev.title = formatShiftTitle(shiftType, ev.meta?.assigned_modality_id);
            return ev;
        }
    },

    async deleteEvent(ev) {
        if (!ev.id) return;
        const res = await execSQL(`DELETE FROM staff_shift_schedules WHERE shift_id = ${ev.id}`);
        if (res && res.error) alert("SQL Error: " + res.error);
    },

    async saveBatch(newEvents, deleteIds) {
        const people = CalendarEngine.getPeople();
        for (const id of deleteIds) {
            await execSQL(`DELETE FROM staff_shift_schedules WHERE shift_id = ${id}`);
        }
        let skipped = 0;
        for (const ev of newEvents) {
            const saved = await this.saveEvent(ev, people[ev.personIdx]);
            if (!saved) skipped++;
        }
        if (skipped > 0) alert(`${skipped} shift(s) were skipped due to overlap or duration constraints.`);
    },

    onEventEdit(ev, people, onDone) {
        window.isEditing = true;
        document.getElementById('editShiftId').value = ev.id;
        document.getElementById('editShiftUser').innerHTML = people.map((p, idx) =>
            `<option value="${idx}" ${idx === ev.personIdx ? 'selected' : ''}>${p.name}</option>`
        ).join('');
        document.getElementById('editShiftFacility').value = ev.meta?.facility_id || (g_facilities[0]?.facility_id || 1);
        document.getElementById('editShiftRoom').value = ev.meta?.assigned_modality_id || '';
        document.getElementById('editShiftType').value = ev.meta?.shift_type || 'WORKING_REGULAR';
        document.getElementById('editShiftNotes').value = ev.meta?.notes || '';

        const modal = document.getElementById('shiftEditModal');
        modal.style.display = 'block';

        document.getElementById('btnSaveShiftEdit').onclick = async () => {
            ev.personIdx = parseInt(document.getElementById('editShiftUser').value, 10);
            ev.meta.facility_id = parseInt(document.getElementById('editShiftFacility').value, 10);
            const rVal = document.getElementById('editShiftRoom').value;
            ev.meta.assigned_modality_id = rVal ? parseInt(rVal, 10) : null;
            ev.meta.shift_type = document.getElementById('editShiftType').value;
            ev.meta.notes = document.getElementById('editShiftNotes').value;

            await StaffScheduleAdapter.saveEvent(ev, people[ev.personIdx]);
            modal.style.display = 'none';
            window.isEditing = false;
            onDone();
        };
        document.getElementById('btnCancelShiftEdit').onclick = () => {
            modal.style.display = 'none';
            window.isEditing = false;
        };
    }
};

window.addEventListener('DOMContentLoaded', () => {
    CalendarEngine.init(StaffScheduleAdapter);
});
