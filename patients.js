/* --- GLOBAL STATE & UTILITIES --- */
let g_apptViewMode = 'table';
let g_orderFilterTimer = null;
let g_slotPatientMap = {};

// Sorting state trackers
let g_orderSortCol = 'ordered_at';
let g_orderSortAsc = false;
let g_patientSortCol = 'patient_id';
let g_patientSortAsc = false;

function clearRebookFilter() {
    document.getElementById('rb-flt-status').value = 'ALL';
    document.getElementById('rb-flt-search').value = '';
    refreshRebookTable();
}

async function execSQL(query) {
    const res = await fetch('/api/query', { method: 'POST', body: query });
    return await res.json();
}

function filterSelect(inputElem, selectId) {
    const filter = inputElem.value.toLowerCase();
    const opts = document.getElementById(selectId).options;
    for (let i = 0; i < opts.length; i++) {
        const txt = opts[i].text.toLowerCase();
        const match = txt.includes(filter);
        opts[i].style.display = match ? '' : 'none';
        opts[i].disabled = !match;
    }
}

function escAttr(v) {
    return (v === null || v === undefined) ? '' : String(v).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
}

/* --- REBOOK & EXCEPTION QUEUE (CANCELLED / NO-SHOW FILTER) --- */
async function loadRebookQueueUI() {
    let html = `
    <div class='header-flex'>
        <h2>Rebook &amp; Exception Queue (Cancelled / No-Show)</h2>
        <button class='btn' style='background:#34495e; margin:0;' onclick='loadAppointments()'>&larr; Back to Appointment Slots</button>
    </div>
    <div style="display:flex; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin:12px 0;">
        <div style="min-width:200px;">
            <label style="font-size:12px; font-weight:bold;">Exception Reason / Status</label>
            <select id="rb-flt-status" onchange="refreshRebookTable()">
                <option value="ALL">All Exceptions (Cancelled &amp; No-Show)</option>
                <option value="CANCELLED" selected>CANCELLED</option>
                <option value="NO_SHOW">NO_SHOW</option>
            </select>
        </div>
        <div style="flex:1;">
            <label style="font-size:12px; font-weight:bold;">Filter by Patient or Accession</label>
            <input type="text" id="rb-flt-search" placeholder="Search patient name or accession..." oninput="scheduleRebookFilter()">
        </div>
        <div>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="clearRebookFilter()">Clear</button>
        </div>
    </div>
    <div id="rebook-table-wrap">Loading...</div>`;

    document.getElementById('main-content').innerHTML = html;
    refreshRebookTable();
}

async function refreshRebookTable() {
    const statusFlt = document.getElementById('rb-flt-status')?.value || 'ALL';
    const search = (document.getElementById('rb-flt-search')?.value || '').trim().replace(/'/g, "''");

    const where = [`s.slot_status IN ('CANCELLED', 'NO_SHOW')`];
    if (statusFlt !== 'ALL') {
        where.push(`s.slot_status = '${statusFlt}'`);
    }
    if (search) {
        where.push(`(p.first_name || ' ' || p.last_name LIKE '%${search}%' OR p.last_name || ', ' || p.first_name LIKE '%${search}%' OR o.accession_number LIKE '%${search}%')`);
    }
    const whereClause = `WHERE ${where.join(' AND ')}`;

    const data = await execSQL(`
        SELECT s.slot_id,
               s.slot_status,
               COALESCE(o.accession_number, 'None / Unlinked') AS accession_number,
               p.patient_id,
               p.last_name || ', ' || p.first_name AS patient_name,
               p.phone_primary,
               m.room_name,
               s.slot_start_time,
               s.slot_end_time,
               o.order_id
        FROM appointment_slots s
        LEFT JOIN imaging_orders o ON s.order_id = o.order_id
        LEFT JOIN patients p ON o.patient_id = p.patient_id
        LEFT JOIN modalities_and_rooms m ON s.modality_id = m.modality_id
        ${whereClause}
        ORDER BY s.slot_start_time DESC
        LIMIT 100
    `);

    const wrap = document.getElementById('rebook-table-wrap');
    if (!wrap) return;
    if (data && data.error) { wrap.innerHTML = `<p style="color:red; font-weight:bold;">Database Error: ${data.error}</p>`; return; }
    if (!Array.isArray(data) || data.length === 0) { wrap.innerHTML = '<p>No cancelled or no-show appointment slots found.</p>'; return; }

    const cellStyle = 'padding:8px; font-size:12px;';
    let html = `<table style="width:100%; border-collapse:collapse;"><thead><tr>
        <th style="${cellStyle} background:#3498db; color:#fff;">Status</th>
        <th style="${cellStyle} background:#3498db; color:#fff;">Accession</th>
        <th style="${cellStyle} background:#3498db; color:#fff;">Patient Name</th>
        <th style="${cellStyle} background:#3498db; color:#fff;">Phone</th>
        <th style="${cellStyle} background:#3498db; color:#fff;">Modality Room</th>
        <th style="${cellStyle} background:#3498db; color:#fff;">Previous Slot Window</th>
        <th style="${cellStyle} background:#3498db; color:#fff;">Actions</th>
    </tr></thead><tbody>`;

    data.forEach(r => {
        const badgeBg = r.slot_status === 'NO_SHOW' ? '#8e44ad' : '#e74c3c';
        html += `<tr style="cursor:pointer;" onclick="renderPatientContactBanner(${r.patient_id || 'null'})">
            <td style="${cellStyle}"><span style="background:${badgeBg}; color:#fff; padding:2px 6px; border-radius:3px; font-weight:bold; font-size:11px;">${r.slot_status}</span></td>
            <td style="${cellStyle}"><b>${r.accession_number}</b></td>
            <td style="${cellStyle}">${r.patient_name || 'Unknown Patient'}</td>
            <td style="${cellStyle}">${r.phone_primary || 'N/A'}</td>
            <td style="${cellStyle}">${r.room_name || 'N/A'}</td>
            <td style="${cellStyle}">${r.slot_start_time} to ${r.slot_end_time}</td>
            <td style="${cellStyle} white-space:nowrap;">
                ${r.order_id ? `<button class='btn' style='background:#2ecc71; padding:4px 8px; font-size:11px; margin:0 4px 0 0;' onclick='event.stopPropagation(); bookSlotUI(${r.order_id})'>Rebook Order</button>` : ''}
                <button class='btn' style='background:#f39c12; padding:4px 8px; font-size:11px; margin:0;' onclick='event.stopPropagation(); editSlotUI("${r.slot_id}")'>Edit Slot</button>
            </td>
        </tr>`;
    });
    wrap.innerHTML = html + '</tbody></table>';
}
function scheduleRebookFilter() {
    clearTimeout(g_rebookFilterTimer);
    g_rebookFilterTimer = setTimeout(refreshRebookTable, 150);
}

function sqlStr(val, nullIfEmpty = false) {
    const s = (val || '').trim();
    if (nullIfEmpty && !s) return 'NULL';
    return `'${s.replace(/'/g, "''")}'`;
}

function sqlNum(val) {
    const s = (val || '').trim();
    return (s === '' || isNaN(Number(s))) ? 'NULL' : String(Number(s));
}

function sqlInt(val) {
    const s = (val || '').trim();
    const n = parseInt(s, 10);
    return (s === '' || isNaN(n)) ? 'NULL' : String(n);
}

function buildTable(data, editableTable = null, idCol = null, editFuncName = null, extraActionName = null, extraActionLabel = null) {
    if (data && data.error) return `<p style="color: red; font-weight: bold;">Database Error: ${data.error}</p>`;
    if (!data || !Array.isArray(data) || data.length === 0) return '<p>No records found.</p>';

    let cols = Object.keys(data[0]);
    let h = '<table><tr>' + cols.map(c => `<th>${c}</th>`).join('') + (editableTable ? '<th>Actions</th>' : '') + '</tr>';
    
    data.forEach(row => {
        h += '<tr>';
        cols.forEach(c => h += `<td>${row[c] !== null ? row[c] : ''}</td>`);
        if (editableTable) {
            h += `<td style="white-space: nowrap;">`;
            if (extraActionName) {
                h += `<button class='btn' style='background: #9b59b6; margin: 0 5px 0 0;' onclick='${extraActionName}("${row[idCol]}")'>${extraActionLabel}</button>`;
            }
            if (editFuncName) {
                h += `<button class='btn' style='background: #f39c12; margin: 0 5px 0 0;' onclick='${editFuncName}("${row[idCol]}")'>Edit</button>`;
            }
            h += `<button class='btn btn-danger' style='margin: 0;' onclick='deleteRecord("${editableTable}", "${idCol}", "${row[idCol]}")'>Delete</button></td>`;
        }
        h += '</tr>';
    });
    return h + '</table>';
}

async function deleteRecord(table, idCol, idVal) {
    if (!confirm('Delete this record?')) return;
    await execSQL(`DELETE FROM ${table} WHERE ${idCol} = '${idVal}'`);
    if (table === 'patients') loadPatients();
    else if (table === 'appointment_slots') { if (g_apptViewMode === 'calendar') loadAppointmentsCalendar(); else loadAppointments(); }
    else if (document.getElementById(`admin-${table}`)) loadAdmin();
}

/* --- WORKFLOW & PERSISTENT PATIENT CONTACT BANNER --- */
function renderWorkflowSteps(activeStep) {
    const steps = ['1. Patient', '2. Imaging Order', '3. Schedule Slot'];
    return `<div style="display:flex; gap:8px; margin:10px 0; font-size:13px;">` +
        steps.map((label, idx) => {
            const n = idx + 1;
            const bg = n === activeStep ? '#2980b9' : (n < activeStep ? '#27ae60' : '#ecf0f1');
            const fg = n <= activeStep ? '#fff' : '#7f8c8d';
            return `<div style="flex:1; text-align:center; padding:6px 10px; border-radius:4px; font-weight:bold; background:${bg}; color:${fg};">${label}</div>`;
        }).join('') + `</div>`;
}

/* --- UPDATED STICKY PATIENT CONTACT BANNER (WITH NO-SHOW COUNT) --- */
async function renderPatientContactBanner(patientId, containerId = 'patient-contact-banner') {
    const el = document.getElementById(containerId);
    if (!el) return;
    if (!patientId) { el.innerHTML = ''; return; }

    const [patRes, noShowRes] = await Promise.all([
        execSQL(`
            SELECT mrn, first_name, last_name, dob, phone_primary, phone_secondary, email,
                   emergency_contact_name, emergency_contact_phone,
                   remind_email, remind_sms, remind_phone, remind_phone_mode
            FROM patients WHERE patient_id = ${patientId}
        `),
        execSQL(`
            SELECT COUNT(*) AS cnt FROM appointment_slots s
            JOIN imaging_orders o ON s.order_id = o.order_id
            WHERE o.patient_id = ${patientId} AND s.slot_status = 'NO_SHOW'
        `)
    ]);

    if (!Array.isArray(patRes) || patRes.length === 0) { el.innerHTML = ''; return; }
    const p = patRes[0];
    const noShowCount = (Array.isArray(noShowRes) && noShowRes[0]?.cnt) ? noShowRes[0].cnt : 0;

    const remFlags = [];
    if (p.remind_email) remFlags.push('Email');
    if (p.remind_sms) remFlags.push('SMS');
    if (p.remind_phone) remFlags.push(`Phone (${p.remind_phone_mode || 'TTS'})`);

    el.innerHTML = `
    <div style="position:sticky; top:0; z-index:15; background:#fef9e7; border:1px solid #f1c40f; border-left:5px solid #e67e22; border-radius:4px; padding:10px 14px; margin:10px 0 14px 0; box-shadow:0 2px 4px rgba(0,0,0,0.08); display:flex; flex-wrap:wrap; gap:18px; align-items:center; font-size:13px;">
        <div>
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">Patient</div>
            <div style="font-weight:bold; font-size:15px; color:#2c3e50;">${p.last_name}, ${p.first_name}</div>
            <div style="font-size:12px; color:#555;">MRN: <b>${p.mrn}</b> | DOB: ${p.dob}</div>
        </div>
        <div>
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">Callback Phone</div>
            <div style="font-weight:bold; font-size:16px; color:#c0392b;">${p.phone_primary || 'None'}</div>
            ${p.phone_secondary ? `<div style="font-size:12px; color:#555;">Alt: ${p.phone_secondary}</div>` : ''}
        </div>
        <div>
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">Email &amp; Reminders</div>
            <div style="color:#2c3e50;">${p.email || '<i>No email</i>'}</div>
            <div style="font-size:12px; color:#27ae60; font-weight:bold;">${remFlags.length ? remFlags.join(', ') : 'No auto-reminders'}</div>
        </div>
        <div>
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">No-Show History</div>
            <div style="font-size:15px; font-weight:bold; color:${noShowCount > 0 ? '#c0392b' : '#27ae60'};">${noShowCount} Recorded</div>
        </div>
        ${(p.emergency_contact_name || p.emergency_contact_phone) ? `
        <div>
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">Emergency Contact</div>
            <div style="color:#2c3e50; font-weight:bold;">${p.emergency_contact_name || 'N/A'}</div>
            <div style="font-size:12px; color:#555;">${p.emergency_contact_phone || ''}</div>
        </div>` : ''}
    </div>`;
}

/* --- DATALIST HELPER --- */
function validateDatalist(inputElem) {
    if (!inputElem || !inputElem.list) return;
    const val = inputElem.value;
    const opts = inputElem.list.options;
    inputElem.dataset.val = '';
    if (inputElem.dataset.duration !== undefined) inputElem.dataset.duration = '';
    if (inputElem.dataset.patientId !== undefined) inputElem.dataset.patientId = '';
    for (let i = 0; i < opts.length; i++) {
        if (opts[i].value === val) {
            inputElem.dataset.val = opts[i].dataset.id || '';
            if (opts[i].dataset.duration) inputElem.dataset.duration = opts[i].dataset.duration;
            if (opts[i].dataset.patientId) inputElem.dataset.patientId = opts[i].dataset.patientId;
            return;
        }
    }
}

/* --- PATIENTS --- */
async function loadPatients() {
    let html = `<div class='header-flex'><h2>Patient Directory</h2><button class='btn' onclick='newPatient()'>+ Register Patient</button></div>`;
    
    // Filter/search toolbar for patients
    html += `
    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin:12px 0;">
        <div style="flex:1; min-width:200px;">
            <label style="font-size:12px; font-weight:bold;">Search Patients</label>
            <input type="text" id="pat-flt-search" placeholder="Name, MRN, or phone..." oninput="schedulePatientsFilter()">
        </div>
        <div>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="clearPatientsFilter()">Clear</button>
        </div>
    </div>
    <div id="patients-table-wrap">Loading...</div>`;

    document.getElementById('main-content').innerHTML = html;
    refreshPatientsTable();
}

let g_patientFilterTimer = null;
function schedulePatientsFilter() {
    clearTimeout(g_patientFilterTimer);
    g_patientFilterTimer = setTimeout(refreshPatientsTable, 150);
}

function clearPatientsFilter() {
    const searchInput = document.getElementById('pat-flt-search');
    if (searchInput) searchInput.value = '';
    refreshPatientsTable();
}

function sortPatientsBy(col) {
    if (g_patientSortCol === col) {
        g_patientSortAsc = !g_patientSortAsc;
    } else {
        g_patientSortCol = col;
        g_patientSortAsc = true;
    }
    refreshPatientsTable();
}

async function refreshPatientsTable() {
    const search = (document.getElementById('pat-flt-search')?.value || '').trim().replace(/'/g, "''");
    
    const where = [];
    if (search) {
        where.push(`(first_name LIKE '%${search}%' OR last_name LIKE '%${search}%' OR mrn LIKE '%${search}%' OR phone_primary LIKE '%${search}%')`);
    }
    const whereClause = where.length ? `WHERE ${where.join(' AND ')}` : '';
    const orderClause = `ORDER BY ${g_patientSortCol} ${g_patientSortAsc ? 'ASC' : 'DESC'}`;

    const data = await execSQL(`
        SELECT patient_id, mrn, first_name, last_name, dob, biological_sex, phone_primary, email
        FROM patients
        ${whereClause}
        ${orderClause}
        LIMIT 200
    `);

    const wrap = document.getElementById('patients-table-wrap');
    if (!wrap) return;
    if (data && data.error) { wrap.innerHTML = `<p style="color:red; font-weight:bold;">Database Error: ${data.error}</p>`; return; }
    if (!Array.isArray(data) || data.length === 0) { wrap.innerHTML = '<p>No patients found.</p>'; return; }

    const cols = Object.keys(data[0]);
    const cellStyle = 'padding:6px 6px; font-size:12px; word-break:break-word; overflow-wrap:anywhere;';
    
    let html = `<table style="width:100%; table-layout:fixed; border-collapse:collapse;"><thead><tr>`;
    
    const colWidths = {
        patient_id: '8%',
        mrn: '15%',
        first_name: '15%',
        last_name: '15%',
        dob: '12%',
        biological_sex: '8%',
        phone_primary: '15%',
        email: '12%'
    };

    cols.forEach(c => {
        const width = colWidths[c] || '10%';
        html += `<th style="width:${width}; ${cellStyle} background:#3498db; color:#fff; cursor:pointer; border-bottom:2px solid #2980b9;" onclick="sortPatientsBy('${c}')">${c.toUpperCase()}</th>`;
    });
    html += `<th style="width:17%; ${cellStyle} background:#3498db; color:#fff; border-bottom:2px solid #2980b9;">Actions</th></tr></thead><tbody>`;

    data.forEach(row => {
        html += `<tr>`;
        cols.forEach(c => {
            html += `<td style="${cellStyle}">${row[c] !== null ? row[c] : ''}</td>`;
        });
        html += `<td style="${cellStyle} white-space:nowrap;">
            <button class='btn' style='background: #9b59b6; padding:3px 6px; font-size:11px; margin:0 2px 0 0;' onclick='createOrderUI("${row.patient_id}")'>Order</button>
            <button class='btn' style='background: #f39c12; padding:3px 6px; font-size:11px; margin:0 2px 0 0;' onclick='editPatientUI("${row.patient_id}")'>Edit</button>
            <button class='btn btn-danger' style='padding:3px 6px; font-size:11px; margin:0;' onclick='deleteRecord("patients", "patient_id", "${row.patient_id}")'>Del</button>
        </td></tr>`;
    });
    wrap.innerHTML = html + '</tbody></table>';
}
function newPatient() {
    renderPatientForm(null);
}

async function editPatientUI(patientId) {
    const patData = await execSQL(`SELECT * FROM patients WHERE patient_id = ${patientId}`);
    if (!patData || patData.error || patData.length === 0) return;
    renderPatientForm(patData[0]);
}

/* --- REPLACE renderPatientForm PORTION FOR PRIMARY CARE MD --- */
async function renderPatientForm(p) {
    const isEdit = !!p;
    const v = (k, def = '') => (p && p[k] !== null && p[k] !== undefined) ? p[k] : def;
    const chk = (k, def = 0) => ((p ? p[k] : def) ? 'checked' : '');
    const sel = (k, opt, def = '') => ((p && p[k] !== null && p[k] !== undefined ? p[k] : def) === opt ? 'selected' : '');

    const gridStyle = 'display:grid; grid-template-columns:repeat(auto-fit, minmax(175px, 1fr)); gap:10px; margin-bottom:12px;';
    const secStyle = 'border:1px solid #ddd; border-radius:4px; padding:12px; margin-bottom:12px; background:#fafafa;';
    const h4Style = 'margin:0 0 10px 0; color:#2c3e50; font-size:13px; border-bottom:1px solid #e0e0e0; padding-bottom:4px;';
    const chkLbl = 'display:inline-flex; align-items:center; gap:5px; margin-right:14px; font-size:13px; cursor:pointer;';

    let html = `
    <div class='header-flex'>
        <h2>${isEdit ? `Edit Patient #${p.patient_id}` : 'Register New Patient'}</h2>
        <button class='btn btn-danger' onclick='loadPatients()'>Cancel</button>
    </div>
    ${renderWorkflowSteps(1)}
    <div style="background:#fff; padding:16px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-top:10px;">
        <div style="${secStyle}">
            <h4 style="${h4Style}">Identity &amp; Demographics</h4>
            <div style="${gridStyle}">
                <div><label>MRN *</label><input type="text" id="p-mrn" value="${escAttr(v('mrn'))}" required></div>
                <div><label>Enterprise MPI ID</label><input type="text" id="p-mpi" value="${escAttr(v('enterprise_mpi_id'))}"></div>
                <div><label>Health Card / SSN</label><input type="text" id="p-hc" value="${escAttr(v('health_card_or_ssn'))}"></div>
                <div><label>Preferred Language</label><input type="text" id="p-lang" value="${escAttr(v('preferred_language', 'en'))}"></div>
                <div><label>First Name *</label><input type="text" id="p-fn" value="${escAttr(v('first_name'))}" required></div>
                <div><label>Middle Name</label><input type="text" id="p-mn" value="${escAttr(v('middle_name'))}"></div>
                <div><label>Last Name *</label><input type="text" id="p-ln" value="${escAttr(v('last_name'))}" required></div>
                <div><label>Preferred Name</label><input type="text" id="p-prefn" value="${escAttr(v('preferred_name'))}"></div>
                <div><label>DOB *</label><input type="date" id="p-dob" value="${escAttr(v('dob'))}" required></div>
                <div><label>Biological Sex *</label>
                    <select id="p-sex">
                        <option value="M" ${sel('biological_sex','M','M')}>Male (M)</option>
                        <option value="F" ${sel('biological_sex','F','M')}>Female (F)</option>
                        <option value="O" ${sel('biological_sex','O','M')}>Other (O)</option>
                        <option value="U" ${sel('biological_sex','U','M')}>Unknown (U)</option>
                    </select>
                </div>
                <div><label>Gender Identity</label><input type="text" id="p-gid" value="${escAttr(v('gender_identity'))}"></div>
                <div><label>Weight (kg)</label><input type="number" step="0.01" id="p-wt" value="${escAttr(v('weight_kg'))}"></div>
                <div><label>Height (cm)</label><input type="number" step="0.01" id="p-ht" value="${escAttr(v('height_cm'))}"></div>
            </div>
        </div>

        <div style="${secStyle}">
            <h4 style="${h4Style}">Contact, Address &amp; Reminders</h4>
            <div style="${gridStyle}">
                <div><label>Primary Phone *</label><input type="text" id="p-ph1" value="${escAttr(v('phone_primary'))}" required></div>
                <div><label>Secondary Phone</label><input type="text" id="p-ph2" value="${escAttr(v('phone_secondary'))}"></div>
                <div><label>Email</label><input type="text" id="p-email" value="${escAttr(v('email'))}"></div>
                <div><label>Preferred Comm Channel</label>
                    <select id="p-comm">
                        <option value="PHONE" ${sel('preferred_comm_channel','PHONE','PHONE')}>PHONE</option>
                        <option value="SMS" ${sel('preferred_comm_channel','SMS','PHONE')}>SMS</option>
                        <option value="EMAIL" ${sel('preferred_comm_channel','EMAIL','PHONE')}>EMAIL</option>
                        <option value="PORTAL" ${sel('preferred_comm_channel','PORTAL','PHONE')}>PORTAL</option>
                    </select>
                </div>
                <div><label>Address (Full / Legacy)</label><input type="text" id="p-addr" value="${escAttr(v('address'))}"></div>
                <div><label>Address Line 1</label><input type="text" id="p-addr1" value="${escAttr(v('address_line1'))}"></div>
                <div><label>Address Line 2</label><input type="text" id="p-addr2" value="${escAttr(v('address_line2'))}"></div>
                <div><label>City</label><input type="text" id="p-city" value="${escAttr(v('city'))}"></div>
                <div><label>State / Province</label><input type="text" id="p-prov" value="${escAttr(v('state_province'))}"></div>
                <div><label>Postal Code</label><input type="text" id="p-post" value="${escAttr(v('postal_code'))}"></div>
                <div><label>Country</label><input type="text" id="p-ctry" value="${escAttr(v('country', 'CA'))}"></div>
            </div>
            <div style="${gridStyle}">
                <div><label>Emergency Contact Name</label><input type="text" id="p-ec-name" value="${escAttr(v('emergency_contact_name'))}"></div>
                <div><label>Emergency Contact Phone</label><input type="text" id="p-ec-phone" value="${escAttr(v('emergency_contact_phone'))}"></div>
                <div><label>Emergency Contact Relation</label><input type="text" id="p-ec-rel" value="${escAttr(v('emergency_contact_relation'))}"></div>
            </div>
            <div style="padding-top:4px;">
                <b style="font-size:12px; margin-right:10px;">Reminders:</b>
                <label style="${chkLbl}"><input type="checkbox" id="p-rem-email" style="width:auto;" ${chk('remind_email', 0)}> Email</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-rem-sms" style="width:auto;" ${chk('remind_sms', 0)}> SMS</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-rem-phone" style="width:auto;" ${chk('remind_phone', 0)}> Phone</label>
                <select id="p-rem-pmode" style="width:auto; display:inline-block; margin-right:18px;">
                    <option value="TTS" ${sel('remind_phone_mode','TTS','TTS')}>Automated TTS</option>
                    <option value="VERBAL" ${sel('remind_phone_mode','VERBAL','TTS')}>Verbal Call (Staff)</option>
                </select>
                <label style="${chkLbl}"><input type="checkbox" id="p-portal" style="width:auto;" ${chk('patient_portal_enabled', 1)}> Portal Enabled</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-vip" style="width:auto;" ${chk('is_VIP_restricted', 0)}> VIP Restricted</label>
            </div>
        </div>

        <div style="${secStyle}">
            <h4 style="${h4Style}">Clinical &amp; Radiology Safety</h4>
            <div style="${gridStyle}">
                <div><label>Allergies</label><input type="text" id="p-alg" value="${escAttr(v('allergies'))}"></div>
                <div><label>Contrast Allergy Details</label><input type="text" id="p-calg-det" value="${escAttr(v('contrast_allergy_details'))}"></div>
                <div><label>MRI Safety Notes</label><input type="text" id="p-mri-notes" value="${escAttr(v('mri_safety_notes'))}"></div>
                <div><label>Pregnancy Status</label>
                    <select id="p-preg">
                        <option value="UNKNOWN" ${sel('pregnancy_status','UNKNOWN','UNKNOWN')}>UNKNOWN</option>
                        <option value="NOT_PREGNANT" ${sel('pregnancy_status','NOT_PREGNANT','UNKNOWN')}>NOT_PREGNANT</option>
                        <option value="PREGNANT" ${sel('pregnancy_status','PREGNANT','UNKNOWN')}>PREGNANT</option>
                        <option value="N/A" ${sel('pregnancy_status','N/A','UNKNOWN')}>N/A</option>
                    </select>
                </div>
                <div><label>eGFR Value</label><input type="number" step="0.01" id="p-egfr" value="${escAttr(v('egfr_value'))}"></div>
                <div><label>Creatinine Value</label><input type="number" step="0.01" id="p-creat" value="${escAttr(v('creatinine_value'))}"></div>
                <div><label>Renal Lab Date</label><input type="date" id="p-rdate" value="${escAttr(v('renal_lab_date'))}"></div>
                <div><label>Isolation Precautions</label><input type="text" id="p-iso" value="${escAttr(v('isolation_precautions'))}"></div>
            </div>
            <div>
                <label style="${chkLbl}"><input type="checkbox" id="p-calg" style="width:auto;" ${chk('contrast_allergy_flag', 0)}> Contrast Allergy</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-mri" style="width:auto;" ${chk('mri_implant_flag', 0)}> MRI Implant</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-claustro" style="width:auto;" ${chk('claustrophobia_flag', 0)}> Claustrophobia</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-fall" style="width:auto;" ${chk('fall_risk_flag', 0)}> Fall Risk</label>
            </div>
        </div>

        <div style="${secStyle}">
            <h4 style="${h4Style}">Insurance, Care Team &amp; Record Lifecycle</h4>
            <div style="${gridStyle}">
                <div><label>Insurance Provider</label><input type="text" id="p-ins-prov" value="${escAttr(v('insurance_provider'))}"></div>
                <div><label>Insurance Policy #</label><input type="text" id="p-ins-pol" value="${escAttr(v('insurance_policy_number'))}"></div>
                <div><label>Insurance Group #</label><input type="text" id="p-ins-grp" value="${escAttr(v('insurance_group_number'))}"></div>
                <div><label>Default Payer ID</label><input type="number" id="p-payer" value="${escAttr(v('default_payer_id'))}"></div>
                <div>
                    <label>Primary Care MD</label>
                    <input type="text" id="p-pcmd-input" list="p-pcmd-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Type MD name...">
                    <datalist id="p-pcmd-list"></datalist>
                </div>
                <div><label>Patient Status</label>
                    <select id="p-status">
                        <option value="ACTIVE" ${sel('patient_status','ACTIVE','ACTIVE')}>ACTIVE</option>
                        <option value="INACTIVE" ${sel('patient_status','INACTIVE','ACTIVE')}>INACTIVE</option>
                        <option value="DECEASED" ${sel('patient_status','DECEASED','ACTIVE')}>DECEASED</option>
                        <option value="MERGED" ${sel('patient_status','MERGED','ACTIVE')}>MERGED</option>
                    </select>
                </div>
                <div><label>Deceased Date</label><input type="date" id="p-dec" value="${escAttr(v('deceased_date'))}"></div>
                <div><label>Merged Into Patient ID</label><input type="number" id="p-merged-id" value="${escAttr(v('merged_into_patient_id'))}"></div>
            </div>
            <div style="margin-bottom:10px;">
                <label style="${chkLbl}"><input type="checkbox" id="p-active" style="width:auto;" ${chk('is_active', 1)}> Active Record</label>
                <label style="${chkLbl}"><input type="checkbox" id="p-merged" style="width:auto;" ${chk('is_merged', 0)}> Is Merged</label>
            </div>
            <div>
                <label>Notes</label>
                <textarea id="p-notes" rows="2">${escAttr(v('notes'))}</textarea>
            </div>
        </div>

        <button class="btn" onclick="submitPatientForm(${isEdit ? p.patient_id : 'null'}, true)">${isEdit ? 'Update' : 'Save'} &amp; Create Order &rarr;</button>
        <button class="btn" style="background:#7f8c8d; margin-left:6px;" onclick="submitPatientForm(${isEdit ? p.patient_id : 'null'}, false)">${isEdit ? 'Update Only' : 'Save Only'}</button>
        <div id="pat-error" style="color:#e74c3c; font-weight:bold; margin-top:10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;

    const mds = await execSQL(`SELECT user_id, first_name, last_name, specialty FROM staff_users WHERE is_active = 1 ORDER BY last_name`);
    if (Array.isArray(mds)) {
        const curMd = v('primary_care_md_id');
        document.getElementById('p-pcmd-list').innerHTML = mds.map(m => {
            const fullName = `Dr. ${m.first_name} ${m.last_name}${m.specialty ? ' (' + m.specialty + ')' : ''}`;
            return `<option value="${fullName}" data-id="${m.user_id}">`;
        }).join('');

        if (curMd) {
            const match = mds.find(m => String(m.user_id) === String(curMd));
            if (match) {
                const inp = document.getElementById('p-pcmd-input');
                inp.value = `Dr. ${match.first_name} ${match.last_name}${match.specialty ? ' (' + match.specialty + ')' : ''}`;
                inp.dataset.val = String(match.user_id);
            }
        }
    }
}

/* --- UPDATED SUBMIT FUNCTION TO AUTO-CREATE NEW MD IF TYPED --- */
async function submitPatientForm(patientId, goToOrder = true) {
    const val = id => document.getElementById(id)?.value || '';
    const isChk = id => document.getElementById(id)?.checked ? 1 : 0;

    if (!val('p-mrn').trim() || !val('p-fn').trim() || !val('p-ln').trim() || !val('p-dob') || !val('p-ph1').trim()) {
        document.getElementById('pat-error').innerText = 'MRN, First Name, Last Name, DOB, and Primary Phone are required.';
        return;
    }

    // Handle Primary Care MD resolution or auto-creation
    const pcmdInput = document.getElementById('p-pcmd-input');
    validateDatalist(pcmdInput);
    let pcmdId = pcmdInput?.dataset.val ? parseInt(pcmdInput.dataset.val, 10) : null;

    const typedMdText = pcmdInput?.value.trim();
    if (!pcmdId && typedMdText) {
        // Parse typed name into first/last name chunks (e.g. "Dr. John Smith" -> First: John, Last: Smith)
        let cleaned = typedMdText.replace(/^Dr\.\s*/i, '');
        let parts = cleaned.split(' ');
        let firstName = parts[0] || 'Unknown';
        let lastName = parts.slice(1).join(' ') || firstName;
        let generatedUsername = `refmd_${Date.now().toString().slice(-5)}`;

        const newMdRes = await execSQL(`
            INSERT INTO staff_users (username, password_hash, first_name, last_name, role_id, is_active)
            VALUES ('${generatedUsername}', 'AUTO_CREATED', '${firstName.replace(/'/g, "''")}', '${lastName.replace(/'/g, "''")}', 6, 1)
        `);
        if (newMdRes && !newMdRes.error) {
            const idFetch = await execSQL(`SELECT MAX(user_id) AS id FROM staff_users WHERE username = '${generatedUsername}'`);
            if (Array.isArray(idFetch) && idFetch[0]?.id) {
                pcmdId = idFetch[0].id;
            }
        }
    }

    const fields = {
        mrn: sqlStr(val('p-mrn')),
        enterprise_mpi_id: sqlStr(val('p-mpi'), true),
        health_card_or_ssn: sqlStr(val('p-hc'), true),
        first_name: sqlStr(val('p-fn')),
        middle_name: sqlStr(val('p-mn'), true),
        last_name: sqlStr(val('p-ln')),
        preferred_name: sqlStr(val('p-prefn'), true),
        dob: sqlStr(val('p-dob')),
        biological_sex: sqlStr(val('p-sex')),
        gender_identity: sqlStr(val('p-gid'), true),
        weight_kg: sqlNum(val('p-wt')),
        height_cm: sqlNum(val('p-ht')),
        phone_primary: sqlStr(val('p-ph1')),
        phone_secondary: sqlStr(val('p-ph2'), true),
        email: sqlStr(val('p-email'), true),
        preferred_comm_channel: sqlStr(val('p-comm')),
        address: sqlStr(val('p-addr'), true),
        address_line1: sqlStr(val('p-addr1'), true),
        address_line2: sqlStr(val('p-addr2'), true),
        city: sqlStr(val('p-city'), true),
        state_province: sqlStr(val('p-prov')),
        postal_code: sqlStr(val('p-post')),
        country: sqlStr(val('p-ctry') || 'CA'),
        preferred_language: sqlStr(val('p-lang') || 'en'),
        emergency_contact_name: sqlStr(val('p-ec-name'), true),
        emergency_contact_phone: sqlStr(val('p-ec-phone'), true),
        emergency_contact_relation: sqlStr(val('p-ec-rel'), true),
        patient_portal_enabled: isChk('p-portal'),
        is_VIP_restricted: isChk('p-vip'),
        insurance_provider: sqlStr(val('p-ins-prov'), true),
        insurance_policy_number: sqlStr(val('p-ins-pol'), true),
        insurance_group_number: sqlStr(val('p-ins-grp'), true),
        default_payer_id: sqlInt(val('p-payer')),
        primary_care_md_id: pcmdId ? String(pcmdId) : 'NULL',
        allergies: sqlStr(val('p-alg'), true),
        contrast_allergy_flag: isChk('p-calg'),
        contrast_allergy_details: sqlStr(val('p-calg-det'), true),
        mri_implant_flag: isChk('p-mri'),
        mri_safety_notes: sqlStr(val('p-mri-notes'), true),
        pregnancy_status: sqlStr(val('p-preg')),
        egfr_value: sqlNum(val('p-egfr')),
        creatinine_value: sqlNum(val('p-creat')),
        renal_lab_date: sqlStr(val('p-rdate'), true),
        claustrophobia_flag: isChk('p-claustro'),
        fall_risk_flag: isChk('p-fall'),
        isolation_precautions: sqlStr(val('p-iso'), true),
        remind_email: isChk('p-rem-email'),
        remind_sms: isChk('p-rem-sms'),
        remind_phone: isChk('p-rem-phone'),
        remind_phone_mode: sqlStr(val('p-rem-pmode')),
        patient_status: sqlStr(val('p-status')),
        deceased_date: sqlStr(val('p-dec'), true),
        is_merged: isChk('p-merged'),
        merged_into_patient_id: sqlInt(val('p-merged-id')),
        is_active: isChk('p-active'),
        notes: sqlStr(val('p-notes'), true)
    };

    const cols = Object.keys(fields);
    const query = patientId
        ? `UPDATE patients SET ${cols.map(c => `${c}=${fields[c]}`).join(', ')}, updated_at=datetime('now') WHERE patient_id=${patientId}`
        : `INSERT INTO patients (${cols.join(', ')}) VALUES (${cols.map(c => fields[c]).join(', ')})`;

    const data = await execSQL(query);
    if (data && data.error) { document.getElementById('pat-error').innerText = data.error; return; }

    if (goToOrder) {
        let targetId = patientId;
        if (!targetId) {
            const idRes = await execSQL(`SELECT patient_id FROM patients WHERE mrn = ${fields.mrn}`);
            if (Array.isArray(idRes) && idRes[0]?.patient_id) targetId = idRes[0].patient_id;
        }
        if (targetId) return bookProcedureUI(targetId);
    }
    loadPatients();
}
/* --- STEP 2: CREATE IMAGING ORDER --- */
function createOrderUI(patientId) {
    bookProcedureUI(patientId);
}

async function bookProcedureUI(preselectPatientId = null) {
    const today = new Date();
    const pad = n => String(n).padStart(2, '0');
    const accDefault = `ACC-${today.getFullYear()}${pad(today.getMonth() + 1)}${pad(today.getDate())}-${String(Date.now()).slice(-4)}`;

    let html = `
    <div class='header-flex'>
        <h2>Create Imaging Order</h2>
        <button class='btn btn-danger' onclick='${preselectPatientId ? 'loadPatients()' : 'loadOrders()'}'>Cancel</button>
    </div>
    ${renderWorkflowSteps(2)}
    <div id="patient-contact-banner"></div>
    <div style="background: #fff; padding: 20px; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); max-width: 650px;">
        <div style="display: flex; gap: 10px;">
            <div style="flex: 1;"><label>Accession Number *</label><input type="text" id="ord-acc" value="${accDefault}" required></div>
            <div style="flex: 1;"><label>Priority *</label>
                <select id="ord-prio">
                    <option value="ROUTINE">ROUTINE</option>
                    <option value="URGENT">URGENT</option>
                    <option value="STAT">STAT</option>
                </select>
            </div>
        </div><br>

        <label>Patient *</label><br>
        <input type="text" id="ord-pat" list="ord-pat-list" onchange="validateDatalist(this); renderPatientContactBanner(this.dataset.val);" onblur="validateDatalist(this); renderPatientContactBanner(this.dataset.val);" placeholder="Search patient by name or MRN..." required>
        <datalist id="ord-pat-list"></datalist><br><br>

        <div style="display: flex; gap: 10px;">
            <div style="flex: 1;"><label>Facility *</label><select id="ord-fac"></select></div>
            <div style="flex: 1;"><label>Modality Room (Optional)</label><select id="ord-room"><option value="NULL">-- Assign Later --</option></select></div>
        </div><br>

        <label>Exam / Procedure *</label><br>
        <input type="text" id="ord-exam" list="ord-exam-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Type or select Exam..." required>
        <datalist id="ord-exam-list"></datalist><br><br>

        <label>Referring Physician (Optional)</label><br>
        <input type="text" id="ord-md" list="ord-md-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Type or select Referring MD...">
        <datalist id="ord-md-list"></datalist><br><br>

        <label>Primary ICD-10 Diagnosis (Optional)</label><br>
        <input type="text" id="ord-icd" list="ord-icd-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Type or select ICD-10 code...">
        <datalist id="ord-icd-list"></datalist><br><br>

        <label>Clinical Indication *</label>
        <textarea id="ord-indication" rows="3" placeholder="Reason for exam / symptoms..." required></textarea><br><br>

        <button class="btn" onclick="submitNewOrder(true)">Save &amp; Schedule Slot &rarr;</button>
        <button class="btn" style="background:#7f8c8d; margin-left:6px;" onclick="submitNewOrder(false)">Save Order Only</button>
        <div id="ord-error" style="color: #e74c3c; font-weight: bold; margin-top: 10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;

    const [pats, facs, rooms, exams, mds, icds] = await Promise.all([
        execSQL(`SELECT patient_id, mrn, first_name, last_name FROM patients ORDER BY last_name, first_name`),
        execSQL(`SELECT facility_id, facility_name FROM facilities WHERE is_active = 1 ORDER BY facility_name`),
        execSQL(`SELECT modality_id, room_name, modality_type FROM modalities_and_rooms WHERE operational_status = 'ONLINE' ORDER BY room_name`),
        execSQL(`SELECT exam_code, exam_name, modality_type FROM exam_catalog ORDER BY exam_code`),
        execSQL(`SELECT user_id, first_name, last_name, specialty FROM staff_users WHERE role_id = 6 AND is_active = 1 ORDER BY last_name`),
        execSQL(`SELECT icd10_code, short_description FROM icd10_diagnoses ORDER BY icd10_code`)
    ]);

    if (Array.isArray(pats)) {
        document.getElementById('ord-pat-list').innerHTML = pats.map(p =>
            `<option value="${p.last_name}, ${p.first_name} (MRN: ${p.mrn})" data-id="${p.patient_id}">`
        ).join('');
        if (preselectPatientId) {
            const match = pats.find(p => String(p.patient_id) === String(preselectPatientId));
            if (match) {
                const patInput = document.getElementById('ord-pat');
                patInput.value = `${match.last_name}, ${match.first_name} (MRN: ${match.mrn})`;
                patInput.dataset.val = String(match.patient_id);
                renderPatientContactBanner(match.patient_id);
            }
        }
    }
    if (Array.isArray(facs)) document.getElementById('ord-fac').innerHTML = facs.map(f => `<option value="${f.facility_id}">${f.facility_name}</option>`).join('');
    if (Array.isArray(rooms)) document.getElementById('ord-room').innerHTML += rooms.map(r => `<option value="${r.modality_id}">${r.room_name} [${r.modality_type}]</option>`).join('');
    if (Array.isArray(exams)) document.getElementById('ord-exam-list').innerHTML = exams.map(e => `<option value="${e.exam_code} - ${e.exam_name} [${e.modality_type}]" data-id="${e.exam_code}">`).join('');
    if (Array.isArray(mds)) document.getElementById('ord-md-list').innerHTML = mds.map(m => `<option value="Dr. ${m.first_name} ${m.last_name}${m.specialty ? ' (' + m.specialty + ')' : ''}" data-id="${m.user_id}">`).join('');
    if (Array.isArray(icds)) document.getElementById('ord-icd-list').innerHTML = icds.map(c => `<option value="${c.icd10_code} - ${c.short_description}" data-id="${c.icd10_code}">`).join('');
}

async function submitNewOrder(goToSchedule = true) {
    const patIn = document.getElementById('ord-pat');
    const examIn = document.getElementById('ord-exam');
    const mdIn = document.getElementById('ord-md');
    const icdIn = document.getElementById('ord-icd');
    validateDatalist(patIn);
    validateDatalist(examIn);
    validateDatalist(mdIn);
    validateDatalist(icdIn);

    const acc = document.getElementById('ord-acc').value.trim().replace(/'/g, "''");
    const prio = document.getElementById('ord-prio').value;
    const patId = patIn.dataset.val;
    const facId = document.getElementById('ord-fac').value || 1;
    const roomId = document.getElementById('ord-room').value || 'NULL';
    const examCode = (examIn.dataset.val || '').replace(/'/g, "''");
    const mdId = mdIn.dataset.val || 'NULL';
    const icdCode = (icdIn.dataset.val || '').replace(/'/g, "''");
    const indication = document.getElementById('ord-indication').value.trim().replace(/'/g, "''");

    const errEl = document.getElementById('ord-error');
    if (!acc || !patId || !examCode || !indication) {
        errEl.innerText = "Accession, valid Patient, valid Exam, and Clinical Indication are required.";
        return;
    }

    const res = await execSQL(`
        INSERT INTO imaging_orders (accession_number, facility_id, patient_id, referring_md_id, exam_code, modality_id, clinical_indication, priority, workflow_status)
        VALUES ('${acc}', ${facId}, ${patId}, ${mdId}, '${examCode}', ${roomId}, '${indication}', '${prio}', 'ORDERED')
    `);
    if (res && res.error) { errEl.innerText = res.error; return; }

    const idRes = await execSQL(`SELECT order_id FROM imaging_orders WHERE accession_number = '${acc}'`);
    const orderId = Array.isArray(idRes) && idRes[0]?.order_id;
    if (orderId) {
        if (icdCode) {
            await execSQL(`INSERT OR IGNORE INTO order_diagnoses (order_id, icd10_code, is_primary) VALUES (${orderId}, '${icdCode}', 1)`);
        }
        await execSQL(`
            INSERT INTO workflow_state_history (order_id, previous_status, new_status, transition_notes)
            VALUES (${orderId}, NULL, 'ORDERED', 'Order created via RIS web UI')
        `);
        if (goToSchedule) return bookSlotUI(orderId);
    }

    loadOrders();
}

/* --- IMAGING ORDERS WITH FILTERS & SORTING --- */
function loadOrders() {
    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Imaging Orders</h2>
        <button class='btn' onclick='bookProcedureUI()'>+ Create Order</button>
    </div>
    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin:12px 0;">
        <div style="flex:1; min-width:160px;">
            <label style="font-size:12px; font-weight:bold;">Patient Name</label>
            <input type="text" id="ord-flt-pat" placeholder="First or last name..." oninput="scheduleOrdersFilter()">
        </div>
        <div style="flex:1; min-width:160px;">
            <label style="font-size:12px; font-weight:bold;">Accession Number</label>
            <input type="text" id="ord-flt-acc" placeholder="e.g. ACC-2026..." oninput="scheduleOrdersFilter()">
        </div>
        <div style="min-width:140px;">
            <label style="font-size:12px; font-weight:bold;">Status</label>
            <select id="ord-flt-status" onchange="refreshOrdersTable()">
                <option value="ALL">All Statuses</option>
                <option value="ORDERED">ORDERED</option>
                <option value="SCHEDULED">SCHEDULED</option>
                <option value="CANCELLED">CANCELLED</option>
            </select>
        </div>
        <div style="min-width:130px;">
            <label style="font-size:12px; font-weight:bold;">Priority</label>
            <select id="ord-flt-prio" onchange="refreshOrdersTable()">
                <option value="ALL">All Priorities</option>
                <option value="ROUTINE">ROUTINE</option>
                <option value="URGENT">URGENT</option>
                <option value="STAT">STAT</option>
            </select>
        </div>
        <div style="min-width:140px;">
            <label style="font-size:12px; font-weight:bold;">Reminder Status</label>
            <select id="ord-flt-rem" onchange="refreshOrdersTable()">
                <option value="ALL">All Reminders</option>
                <option value="SENT">Reminder Sent</option>
                <option value="UNSENT">Reminder Unsent</option>
            </select>
        </div>
        <div>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="clearOrdersFilter()">Clear</button>
        </div>
    </div>
    <div id="orders-table-wrap">Loading...</div>`;

    refreshOrdersTable();
}

function scheduleOrdersFilter() {
    clearTimeout(g_orderFilterTimer);
    g_orderFilterTimer = setTimeout(refreshOrdersTable, 150);
}

function clearOrdersFilter() {
    document.getElementById('ord-flt-pat').value = '';
    document.getElementById('ord-flt-acc').value = '';
    document.getElementById('ord-flt-status').value = 'ALL';
    document.getElementById('ord-flt-prio').value = 'ALL';
    document.getElementById('ord-flt-rem').value = 'ALL';
    refreshOrdersTable();
}

function sortOrdersBy(col) {
    if (g_orderSortCol === col) {
        g_orderSortAsc = !g_orderSortAsc;
    } else {
        g_orderSortCol = col;
        g_orderSortAsc = true;
    }
    refreshOrdersTable();
}

async function refreshOrdersTable() {
    const pat = (document.getElementById('ord-flt-pat')?.value || '').trim().replace(/'/g, "''");
    const acc = (document.getElementById('ord-flt-acc')?.value || '').trim().replace(/'/g, "''");
    const status = document.getElementById('ord-flt-status')?.value || 'ALL';
    const prio = document.getElementById('ord-flt-prio')?.value || 'ALL';
    const rem = document.getElementById('ord-flt-rem')?.value || 'ALL';

    const remExistsSql = `EXISTS (
        SELECT 1 FROM correspondence_dispatch_queue q
        WHERE q.order_id = o.order_id
          AND q.message_type = 'APPT_REMINDER'
          AND q.delivery_status IN ('SENT', 'DELIVERED_ACK', 'ACKNOWLEDGED')
    )`;

    const where = [];
    if (pat) where.push(`(p.first_name || ' ' || p.last_name LIKE '%${pat}%' OR p.last_name || ', ' || p.first_name LIKE '%${pat}%')`);
    if (acc) where.push(`o.accession_number LIKE '%${acc}%'`);
    if (status !== 'ALL') where.push(`o.workflow_status = '${status}'`);
    if (prio !== 'ALL') where.push(`o.priority = '${prio}'`);
    if (rem === 'SENT') where.push(remExistsSql);
    else if (rem === 'UNSENT') where.push(`NOT ${remExistsSql}`);

    const whereClause = where.length ? `WHERE ${where.join(' AND ')}` : '';
    const orderClause = `ORDER BY ${g_orderSortCol} ${g_orderSortAsc ? 'ASC' : 'DESC'}`;

    const data = await execSQL(`
        SELECT o.order_id,
               o.accession_number,
               p.last_name || ', ' || p.first_name AS patient_name,
               o.exam_code,
               o.priority,
               o.workflow_status,
               COALESCE(o.scheduled_at, '') AS scheduled_at,
               CASE WHEN ${remExistsSql} THEN 'SENT' ELSE 'UNSENT' END AS reminder_status,
               o.clinical_indication
        FROM imaging_orders o
        JOIN patients p ON o.patient_id = p.patient_id
        ${whereClause}
        ${orderClause}
        LIMIT 100
    `);

    const wrap = document.getElementById('orders-table-wrap');
    if (!wrap) return;
    if (data && data.error) { wrap.innerHTML = `<p style="color:red; font-weight:bold;">Database Error: ${data.error}</p>`; return; }
    if (!Array.isArray(data) || data.length === 0) { wrap.innerHTML = '<p>No matching orders found.</p>'; return; }

    const cols = Object.keys(data[0]);
    const cellStyle = 'padding:6px 5px; font-size:12px; word-break:break-word; overflow-wrap:anywhere;';
    const btnStyle = 'padding:3px 6px; font-size:11px; margin:1px 2px 1px 0;';

    let html = `<div style="font-size:11px; color:#7f8c8d; margin-bottom:4px;">Tip: Double-click any row to open Workflow &amp; Reminder History. Click any column header to sort.</div>
    <table style="width:100%; table-layout:fixed; border-collapse:collapse;">
        <thead><tr>`;
    
    const colLabels = {
        order_id: 'ID',
        accession_number: 'Accession',
        patient_name: 'Patient',
        exam_code: 'Exam',
        priority: 'Priority',
        workflow_status: 'Status',
        scheduled_at: 'Scheduled',
        reminder_status: 'Reminder',
        clinical_indication: 'Indication'
    };
    const colWidths = {
        order_id: '5%',
        accession_number: '12%',
        patient_name: '13%',
        exam_code: '8%',
        priority: '8%',
        workflow_status: '9%',
        scheduled_at: '12%',
        reminder_status: '8%',
        clinical_indication: '11%'
    };

    cols.forEach(c => {
        const label = colLabels[c] || c;
        const width = colWidths[c] || '10%';
        html += `<th style="width:${width}; ${cellStyle} background:#3498db; color:#fff; cursor:pointer; border-bottom:2px solid #2980b9;" onclick="sortOrdersBy('${c}')">${label.toUpperCase()}</th>`;
    });
    html += `<th style="width:14%; ${cellStyle} background:#3498db; color:#fff; border-bottom:2px solid #2980b9;">Actions</th></tr></thead><tbody>`;

    data.forEach(row => {
        const remBg = row.reminder_status === 'SENT' ? '#27ae60' : '#e67e22';
        html += `<tr style="cursor:pointer;" title="Double-click to view history" ondblclick='viewOrderHistory(${row.order_id}, "${row.accession_number}")'>`;
        cols.forEach(c => {
            if (c === 'reminder_status') {
                html += `<td style="${cellStyle}"><span style="background:${remBg}; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px; font-weight:bold;">${row[c]}</span></td>`;
            } else {
                html += `<td style="${cellStyle}">${row[c] !== null && row[c] !== undefined ? row[c] : ''}</td>`;
            }
        });
        html += `<td style="${cellStyle}">
            <button class='btn' style='background:#f39c12; ${btnStyle}' onclick='event.stopPropagation(); bookSlotUI(${row.order_id})'>Schedule</button>
            <button class='btn' style='background:#2ecc71; ${btnStyle}' onclick='event.stopPropagation(); sendOrderReminders(${row.order_id}, "${row.accession_number}")'>Remind</button>
            <button class='btn' style='background:#8e44ad; ${btnStyle}' onclick='event.stopPropagation(); logVerbalCallUI(${row.order_id}, "${row.accession_number}")'>Call</button>
        </td></tr>`;
    });
    wrap.innerHTML = html + '</tbody></table>';
}

async function sendOrderReminders(orderId, accNumber) {
    const infoRes = await execSQL(`
        SELECT o.order_id, o.accession_number, COALESCE(o.scheduled_at, 'Unscheduled') AS scheduled_at,
               p.patient_id, p.first_name, p.last_name, p.phone_primary, p.email,
               p.remind_email, p.remind_sms, p.remind_phone, COALESCE(p.remind_phone_mode, 'TTS') AS remind_phone_mode,
               COALESCE(e.exam_name, o.exam_code) AS exam_name, COALESCE(m.room_name, 'Main Dept') AS room_name
        FROM imaging_orders o
        JOIN patients p ON o.patient_id = p.patient_id
        LEFT JOIN exam_catalog e ON o.exam_code = e.exam_code
        LEFT JOIN modalities_and_rooms m ON o.modality_id = m.modality_id
        WHERE o.order_id = ${orderId}
    `);
    if (!Array.isArray(infoRes) || !infoRes.length) return;
    const d = infoRes[0];

    const tplRes = await execSQL(`SELECT setting_value FROM app_settings WHERE setting_key = 'rem_msg_template'`);
    let msg = (Array.isArray(tplRes) && tplRes[0]?.setting_value)
        ? tplRes[0].setting_value
        : 'Hello {patient}, reminder for your {exam} on {datetime} in {room} (Ref: {accession}).';
    msg = msg.replaceAll('{patient}', `${d.first_name} ${d.last_name}`)
             .replaceAll('{exam}', d.exam_name)
             .replaceAll('{datetime}', d.scheduled_at)
             .replaceAll('{room}', d.room_name)
             .replaceAll('{accession}', d.accession_number);
    const safeMsg = msg.replace(/'/g, "''");

    const tasks = [];
    if (d.remind_email && d.email) tasks.push({ channel: 'EMAIL', dest: d.email, auto: true });
    if (d.remind_sms && d.phone_primary) tasks.push({ channel: 'SMS', dest: d.phone_primary, auto: true });
    if (d.remind_phone && d.phone_primary) {
        if (d.remind_phone_mode === 'VERBAL') tasks.push({ channel: 'PHONE_VERBAL', dest: d.phone_primary, auto: false });
        else tasks.push({ channel: 'PHONE_TTS', dest: d.phone_primary, auto: true });
    }

    if (tasks.length === 0) {
        viewOrderHistory(orderId, accNumber, 'No reminder channels enabled (or missing email/phone) for this patient.');
        return;
    }

    let summary = [];
    for (const t of tasks) {
        await execSQL(`
            INSERT INTO correspondence_dispatch_queue
            (order_id, patient_id, recipient_type, channel, destination_endpoint, message_type, payload_content, delivery_status)
            VALUES (${d.order_id}, ${d.patient_id}, 'PATIENT', '${t.channel}', '${t.dest.replace(/'/g, "''")}', 'APPT_REMINDER', '${safeMsg}', 'QUEUED')
        `);
        const idRes = await execSQL(`SELECT MAX(dispatch_id) AS id FROM correspondence_dispatch_queue WHERE order_id = ${d.order_id}`);
        const dispatchId = Array.isArray(idRes) && idRes[0]?.id;

        if (t.auto && dispatchId) {
            const r = await fetch('/api/dispatch_reminder', { method: 'POST', body: String(dispatchId) });
            const rawText = await r.text();
            let json = {};
            try { json = rawText ? JSON.parse(rawText) : {}; } catch (e) {}
            if (!r.ok) {
                summary.push(`${t.channel}: HTTP ${r.status} (recompile/restart ris.exe)`);
            } else {
                summary.push(`${t.channel}: ${json.status || json.error || 'DONE'}`);
            }
        } else {
            summary.push(`${t.channel}: QUEUED (use Log Call to record verbal confirmation)`);
        }
    }
    viewOrderHistory(orderId, accNumber, summary.join(' | '));
}

async function logVerbalCallUI(orderId, accNumber) {
    const [infoRes, staffRes] = await Promise.all([
        execSQL(`SELECT o.patient_id, p.first_name, p.last_name, p.phone_primary FROM imaging_orders o JOIN patients p ON o.patient_id = p.patient_id WHERE o.order_id = ${orderId}`),
        execSQL(`SELECT user_id, first_name, last_name, username FROM staff_users WHERE is_active = 1 AND role_id != 6 ORDER BY last_name`)
    ]);
    if (!Array.isArray(infoRes) || !infoRes.length) return;
    const p = infoRes[0];
    const staffOpts = (Array.isArray(staffRes) ? staffRes : []).map(u => `<option value="${u.user_id}">${u.first_name} ${u.last_name} (${u.username})</option>`).join('');

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Log Verbal Phone Reminder (${accNumber})</h2>
        <button class='btn btn-danger' onclick='viewOrderHistory(${orderId}, "${accNumber}")'>Cancel</button>
    </div>
    <div style="background:#fff; padding:20px; max-width:600px; margin-top:15px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <p style="margin-top:0;"><b>Patient:</b> ${p.first_name} ${p.last_name} &nbsp;|&nbsp; <b>Phone:</b> ${p.phone_primary || 'N/A'}</p>
        <label>Staff Member Who Made Call *</label>
        <select id="vc-staff">${staffOpts}</select><br><br>
        <label>Outcome / Status *</label>
        <select id="vc-status">
            <option value="ACKNOWLEDGED">ACKNOWLEDGED (Patient confirmed)</option>
            <option value="SENT">SENT (Left voicemail)</option>
            <option value="FAILED">FAILED (No answer / wrong number)</option>
        </select><br><br>
        <label>Call Notes</label>
        <textarea id="vc-notes" rows="3" placeholder="e.g., Confirmed arrival time and fasting prep with patient."></textarea><br><br>
        <button class="btn" onclick="submitVerbalCall(${orderId}, ${p.patient_id}, '${(p.phone_primary || '').replace(/'/g, "''")}', '${accNumber}')">Record Verbal Call</button>
        <div id="vc-error" style="color:#e74c3c; font-weight:bold; margin-top:8px;"></div>
    </div>`;
}

async function submitVerbalCall(orderId, patientId, phone, accNumber) {
    const staffId = document.getElementById('vc-staff').value;
    const status = document.getElementById('vc-status').value;
    const notes = (document.getElementById('vc-notes').value.trim() || 'Verbal appointment reminder call').replace(/'/g, "''");
    if (!staffId) return document.getElementById('vc-error').innerText = 'Select the staff member who placed the call.';

    const res = await execSQL(`
        INSERT INTO correspondence_dispatch_queue
        (order_id, patient_id, recipient_type, channel, destination_endpoint, message_type, payload_content, delivery_status, handled_by_user_id, sent_at)
        VALUES (${orderId}, ${patientId}, 'PATIENT', 'PHONE_VERBAL', '${phone}', 'APPT_REMINDER', '${notes}', '${status}', ${staffId}, CURRENT_TIMESTAMP)
    `);
    if (res && res.error) document.getElementById('vc-error').innerText = res.error;
    else viewOrderHistory(orderId, accNumber, 'Verbal call recorded.');
}

async function viewOrderHistory(orderId, accNumber, statusMsg = '') {
    let html = `<div class='header-flex'>
        <h2>History: ${accNumber}</h2>
        <div>
            <button class='btn' style='background:#2ecc71; margin-right:6px;' onclick='sendOrderReminders(${orderId}, "${accNumber}")'>Send Reminder</button>
            <button class='btn' style='background:#8e44ad; margin-right:6px;' onclick='logVerbalCallUI(${orderId}, "${accNumber}")'>Log Verbal Call</button>
            <button class='btn btn-danger' onclick='loadOrders()'>Back</button>
        </div>
    </div>
    ${statusMsg ? `<div style="margin-top:10px; padding:8px 12px; background:#eafaf1; border:1px solid #2ecc71; border-radius:4px; font-weight:bold; color:#1e8449;">${statusMsg}</div>` : ''}
    <div style="margin-top: 20px;"><h3 style="color: #2c3e50; border-bottom: 2px solid #3498db;">Workflow Audit Trail</h3><div id="audit-table">Loading...</div></div>
    <div style="margin-top: 30px;"><h3 style="color: #2c3e50; border-bottom: 2px solid #2ecc71;">Contact & Reminder History</h3><div id="contact-table">Loading...</div></div>`;
    document.getElementById('main-content').innerHTML = html;

    const auditData = await execSQL(`
        SELECT h.transition_timestamp AS "Time", COALESCE(h.previous_status, 'NEW') AS "Old Status",
               h.new_status AS "New Status", COALESCE(u.username, 'SYSTEM') AS "User", h.transition_notes AS "Notes"
        FROM workflow_state_history h
        LEFT JOIN staff_users u ON h.changed_by_user_id = u.user_id
        WHERE h.order_id = ${orderId} ORDER BY h.transition_timestamp DESC
    `);
    document.getElementById('audit-table').innerHTML = buildTable(auditData);

    const contactData = await execSQL(`
        SELECT q.queued_at AS "Queued", COALESCE(q.sent_at, '') AS "Sent", q.channel AS "Channel",
               q.destination_endpoint AS "Destination", q.message_type AS "Type",
               q.delivery_status AS "Status",
               COALESCE(u.first_name || ' ' || u.last_name, 'AUTO') AS "Handled By",
               q.payload_content AS "Message / Notes"
        FROM correspondence_dispatch_queue q
        LEFT JOIN staff_users u ON q.handled_by_user_id = u.user_id
        WHERE q.order_id = ${orderId} ORDER BY q.queued_at DESC
    `);
    document.getElementById('contact-table').innerHTML = buildTable(contactData);
}

/* --- APPOINTMENTS & SLOTS --- */
async function loadAppointments() {
    g_apptViewMode = 'table';
    let html = `
    <div class='header-flex'>
        <h2>Appointment Slots</h2>
        <div>
            <button class='btn' style='background:#8e44ad; margin-right:6px;' onclick='loadRebookQueueUI()'>Rebook &amp; Exceptions</button>
            <button class='btn' style='background:#3498db; margin-right:6px;' onclick='loadAppointmentsCalendar()'>Calendar View</button>
            <button class='btn' onclick='bookSlotUI()'>+ Book Slot</button>
        </div>
    </div>
    <div id="patient-contact-banner"></div>`;
    const data = await execSQL(`
        SELECT s.slot_id, s.slot_start_time, s.slot_end_time, s.slot_status,
               m.room_name, o.accession_number, o.patient_id,
               CASE WHEN p.patient_id IS NOT NULL THEN p.last_name || ', ' || p.first_name ELSE '' END AS patient
        FROM appointment_slots s
        LEFT JOIN modalities_and_rooms m ON s.modality_id = m.modality_id
        LEFT JOIN imaging_orders o ON s.order_id = o.order_id
        LEFT JOIN patients p ON o.patient_id = p.patient_id
        ORDER BY s.slot_start_time DESC LIMIT 100
    `);

    if (!Array.isArray(data) || data.length === 0) {
        document.getElementById('main-content').innerHTML = html + '<p>No records found.</p>';
        return;
    }

    html += `<table><tr><th>ID</th><th>Start</th><th>End</th><th>Status</th><th>Room</th><th>Accession</th><th>Patient</th><th>Actions</th></tr>`;
    data.forEach(r => {
        html += `<tr style="cursor:pointer;" onclick="renderPatientContactBanner(${r.patient_id || 'null'})">
            <td>${r.slot_id}</td>
            <td>${r.slot_start_time || ''}</td>
            <td>${r.slot_end_time || ''}</td>
            <td>${r.slot_status || ''}</td>
            <td>${r.room_name || ''}</td>
            <td>${r.accession_number || ''}</td>
            <td>${r.patient || ''}</td>
            <td style="white-space:nowrap;">
                <button class='btn' style='background:#f39c12; margin:0 5px 0 0;' onclick='event.stopPropagation(); editSlotUI("${r.slot_id}")'>Edit</button>
                <button class='btn btn-danger' style='margin:0;' onclick='event.stopPropagation(); deleteRecord("appointment_slots", "slot_id", "${r.slot_id}")'>Delete</button>
            </td>
        </tr>`;
    });
    document.getElementById('main-content').innerHTML = html + '</table>';
}

function slotToSqlDateTime(dateSlash, totalMins) {
    const [y, m, d] = dateSlash.split('/').map(Number);
    const dt = new Date(y, m - 1, d, 0, 0, 0);
    dt.setMinutes(totalMins);
    const pad = n => String(n).padStart(2, '0');
    return `${dt.getFullYear()}-${pad(dt.getMonth() + 1)}-${pad(dt.getDate())} ${pad(dt.getHours())}:${pad(dt.getMinutes())}:00`;
}

function slotFromSqlRange(startStr, endStr) {
    const s = new Date(startStr.replace(' ', 'T'));
    const e = new Date(endStr.replace(' ', 'T'));
    const pad = n => String(n).padStart(2, '0');
    const date = `${s.getFullYear()}/${pad(s.getMonth() + 1)}/${pad(s.getDate())}`;
    const startMin = s.getHours() * 60 + s.getMinutes();
    let duration = Math.round((e - s) / 60000);
    if (isNaN(duration) || duration < 15) duration = 30;
    if (duration > 1440 - startMin) duration = Math.max(15, 1440 - startMin);
    return { date, startMin, duration };
}

const SLOT_COLORS = {
    AVAILABLE: '#2ecc71',
    BOOKED: '#039BE5',
    BLOCKED_MAINTENANCE: '#e74c3c',
    RESERVED_STAT: '#f39c12',
    COMPLETED: '#7f8c8d',
    CANCELLED: '#95a5a6',
    NO_SHOW: '#8e44ad'
};
const AppointmentSlotAdapter = {
    defaultViewMode: 4,

    async loadData() {
        const [rooms, slots] = await Promise.all([
            execSQL(`SELECT modality_id, room_name, modality_type FROM modalities_and_rooms ORDER BY room_name`),
            execSQL(`
                SELECT s.slot_id, s.modality_id, s.order_id, s.slot_start_time, s.slot_end_time, s.slot_status,
                       o.accession_number, o.patient_id, p.last_name, p.first_name, e.exam_name
                FROM appointment_slots s
                LEFT JOIN imaging_orders o ON s.order_id = o.order_id
                LEFT JOIN patients p ON o.patient_id = p.patient_id
                LEFT JOIN exam_catalog e ON o.exam_code = e.exam_code
                ORDER BY s.slot_start_time
            `)
        ]);

        const people = (Array.isArray(rooms) ? rooms : []).map(r => ({
            id: r.modality_id,
            name: `${r.room_name} [${r.modality_type}]`,
            meta: r
        }));
        const roomIdxMap = new Map(people.map((r, idx) => [r.id, idx]));

        g_slotPatientMap = {};
        const events = [];
        (Array.isArray(slots) ? slots : []).forEach(s => {
            g_slotPatientMap[String(s.slot_id)] = s.patient_id || null;
            const pIdx = roomIdxMap.get(s.modality_id);
            if (pIdx === undefined) return;
            const { date, startMin, duration } = slotFromSqlRange(s.slot_start_time, s.slot_end_time);
            const title = s.accession_number
                ? `${s.last_name || ''}, ${s.first_name || ''}${s.exam_name ? ' (' + s.exam_name + ')' : ''}`
                : `[${s.slot_status}]`;

            events.push({
                id: String(s.slot_id),
                personIdx: pIdx,
                date,
                startMin,
                duration,
                title,
                footer: s.accession_number || '',
                color: SLOT_COLORS[s.slot_status] || '#039BE5',
                meta: {
                    order_id: s.order_id,
                    patient_id: s.patient_id,
                    slot_status: s.slot_status
                }
            });
        });

        return { people, events };
    },

    createDefaultEvent(personIdx, roomObj, dateStr, startMin, _defaultDur, _color) {
        const status = document.getElementById('comboDefaultEvent')?.value || 'AVAILABLE';
        return {
            id: null,
            personIdx,
            date: dateStr,
            startMin,
            duration: 30,
            title: `[${status}]`,
            color: SLOT_COLORS[status] || '#2ecc71',
            meta: { order_id: null, patient_id: null, slot_status: status }
        };
    },

    async saveEvent(ev, roomObj) {
        const rooms = CalendarEngine.getPeople();
        const room = roomObj || rooms[ev.personIdx];
        if (!room) return null;

        const startStr = slotToSqlDateTime(ev.date, ev.startMin);
        const endStr = slotToSqlDateTime(ev.date, ev.startMin + ev.duration);
        const status = ev.meta?.slot_status || 'AVAILABLE';
        const orderId = ev.meta?.order_id ? ev.meta.order_id : 'NULL';
        const excludeClause = ev.id ? `AND slot_id != ${ev.id}` : '';
        const errDiv = document.getElementById('cal-slot-err');
        if (errDiv) errDiv.innerText = '';

        const roomOverlap = await execSQL(`
            SELECT slot_id FROM appointment_slots
            WHERE modality_id = ${room.id} ${excludeClause}
              AND slot_status NOT IN ('CANCELLED', 'COMPLETED')
              AND ('${startStr}' < slot_end_time AND '${endStr}' > slot_start_time)
        `);
        if (Array.isArray(roomOverlap) && roomOverlap.length > 0) {
            if (errDiv) errDiv.innerText = 'Overlap conflict: room already occupied in that window.';
            await CalendarEngine.refresh();
            return null;
        }

        if (ev.meta?.patient_id) {
            const patOverlap = await execSQL(`
                SELECT s.slot_id FROM appointment_slots s
                JOIN imaging_orders o ON s.order_id = o.order_id
                WHERE o.patient_id = ${ev.meta.patient_id} ${ev.id ? `AND s.slot_id != ${ev.id}` : ''}
                  AND s.slot_status NOT IN ('CANCELLED', 'COMPLETED')
                  AND ('${startStr}' < s.slot_end_time AND '${endStr}' > s.slot_start_time)
            `);
            if (Array.isArray(patOverlap) && patOverlap.length > 0) {
                if (errDiv) errDiv.innerText = 'Overlap conflict: patient already booked in that window.';
                await CalendarEngine.refresh();
                return null;
            }
        }

        if (ev.id) {
            const res = await execSQL(`
                UPDATE appointment_slots
                SET modality_id = ${room.id}, slot_start_time = '${startStr}', slot_end_time = '${endStr}', slot_status = '${status}'
                WHERE slot_id = ${ev.id}
            `);
            if (res && res.error) { if (errDiv) errDiv.innerText = res.error; await CalendarEngine.refresh(); return null; }
            if (orderId !== 'NULL') {
                await execSQL(`UPDATE imaging_orders SET modality_id = ${room.id}, scheduled_at = '${startStr}' WHERE order_id = ${orderId}`);
            }
            return ev;
        } else {
            const res = await execSQL(`
                INSERT INTO appointment_slots (modality_id, order_id, slot_start_time, slot_end_time, slot_status)
                VALUES (${room.id}, ${orderId}, '${startStr}', '${endStr}', '${status}')
            `);
            if (res && res.error) { if (errDiv) errDiv.innerText = res.error; return null; }
            const idRes = await execSQL(`SELECT MAX(slot_id) AS id FROM appointment_slots WHERE modality_id = ${room.id} AND slot_start_time = '${startStr}'`);
            ev.id = (Array.isArray(idRes) && idRes[0]?.id) ? String(idRes[0].id) : String(Date.now());
            return ev;
        }
    },

    async deleteEvent(ev) {
        if (!ev.id) return;
        if (ev.meta?.order_id) {
            await execSQL(`
                INSERT INTO workflow_state_history (order_id, previous_status, new_status, transition_notes, transition_timestamp)
                VALUES (${ev.meta.order_id}, '${ev.meta.slot_status || 'SCHEDULED'}', 'CANCELLED', 'Appointment slot #${ev.id} deleted from calendar', datetime('now'))
            `);
            await execSQL(`UPDATE imaging_orders SET workflow_status = 'CANCELLED', scheduled_at = NULL WHERE order_id = ${ev.meta.order_id}`);
        }
        await execSQL(`DELETE FROM appointment_slots WHERE slot_id = ${ev.id}`);
    },

    onEventEdit(ev) {
        editSlotUI(ev.id);
    }
};

let g_rebookFilterTimer = null;
async function loadAppointmentsCalendar() {
    g_apptViewMode = 'calendar';
    // Title removed entirely, compact spacing applied to fit onscreen
    document.getElementById('main-content').innerHTML = `
    <div style="display:flex; justify-content:flex-end; margin-bottom:4px;">
        <div>
            <button class='btn' style='background:#34495e; padding:6px 10px; margin-right:6px; margin-top:0;' onclick='loadAppointments()'>Table View</button>
            <button class='btn' style='padding:6px 10px; margin-top:0;' onclick='bookSlotUI()'>+ Book Slot</button>
        </div>
    </div>
    <div id="cal-slot-err" style="color:#e74c3c; font-weight:bold; margin-top:2px; min-height:14px; font-size:11px;"></div>
    <div id="patient-contact-banner"></div>
    <div class="cal-wrap">
        <div class="cal-header">
            <select id="comboView">
                <option value="4" selected>Room Columns (Day)</option>
                <option value="1">1 Day View</option>
                <option value="2">4 Day View</option>
                <option value="3">Week View</option>
                <option value="6">Month View</option>
                <option value="7">Upcoming</option>
            </select>
            <div id="lblDateTitleMobile" style="flex:1; text-align:right; font-weight:bold; font-size:14px;"></div>
            <div id="lblDateTitle" style="display:none;"></div>
        </div>
        <div id="toolbar" class="cal-toolbar" data-open="true">
            <label style="display:flex; align-items:center; gap:4px; font-size:11px; font-weight:600; cursor:pointer;">
                <input type="checkbox" id="chkEditMode" checked> Edit
            </label>
            <button id="btnZoomIn">+</button>
            <button id="btnZoomOut">-</button>
            <button id="btnPrev">&lt; Prev</button>
            <button id="btnPrevDay">&lt;</button>
            <button id="btnNextDay">&gt;</button>
            <button id="btnNext">Next &gt;</button>
            <select id="comboDefaultEvent" title="Double-click grid slot status">
                <option value="AVAILABLE">AVAILABLE</option>
                <option value="BLOCKED_MAINTENANCE">BLOCKED_MAINTENANCE</option>
                <option value="RESERVED_STAT">RESERVED_STAT</option>
                <option value="BOOKED">BOOKED</option>
            </select>
            <button id="btnFilterPeople">Filter Rooms</button>
            <select id="comboTempPersonFilter"><option value="">All Rooms</option></select>
            <button id="btnCopy">Copy</button>
            <button id="btnDelete" style="color:#d93025;">Delete</button>
        </div>
        <div id="canvas-container">
            <div id="header-row"></div>
            <div id="scroll-area">
                <div id="timeline-labels"></div>
                <div id="grid-bg"></div>
                <div id="events-layer"></div>
            </div>
        </div>
    </div>
    <div id="peopleFilterModal" class="cal-modal">
        <h3 style="margin-top:0;">Select Visible Rooms</h3>
        <div id="peopleFilterList" class="chk-list"></div>
        <div style="display:flex; justify-content:flex-end; gap:8px;">
            <button class="btn btn-danger" style="margin:0; padding:6px 12px;" onclick="document.getElementById('peopleFilterModal').style.display='none'">Close</button>
            <button id="btnSavePeopleFilter" class="btn" style="margin:0; padding:6px 12px;">Save</button>
        </div>
    </div>`;

    await CalendarEngine.init(AppointmentSlotAdapter);

    const onSelectCard = (e) => {
        const card = e.target.closest('.event-card');
        if (!card) return;
        renderPatientContactBanner(g_slotPatientMap[card.dataset.id] || null);
    };
    const layer = document.getElementById('events-layer');
    layer?.addEventListener('mousedown', onSelectCard);
    layer?.addEventListener('touchstart', onSelectCard, { passive: true });
}
function calcSlotEnd() {
    const orderIn = document.getElementById('slot-order');
    const startIn = document.getElementById('slot-start').value;
    if (!orderIn || !startIn) return;
    const dur = parseInt(orderIn.dataset.duration || '30', 10);
    const d = new Date(startIn);
    if (isNaN(d.getTime())) return;
    d.setMinutes(d.getMinutes() + dur);
    const pad = n => n.toString().padStart(2, '0');
    document.getElementById('slot-end').value = `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

async function bookSlotUI(preselectOrderId = null) {
    let html = `
    <div class='header-flex'>
        <h2>Schedule Appointment Slot</h2>
        <button class='btn btn-danger' onclick='${g_apptViewMode === 'calendar' ? 'loadAppointmentsCalendar()' : 'loadAppointments()'}'>Cancel</button>
    </div>
    ${renderWorkflowSteps(3)}
    <div id="patient-contact-banner"></div>
    <div style="background: #fff; padding: 20px; max-width: 650px; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.1);">
        <label>Modality Room *</label><br>
        <input type="text" id="slot-room" list="slot-room-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Type or select Room..." required>
        <datalist id="slot-room-list"></datalist><br><br>

        <label>Linked Order / Accession (Optional)</label><br>
        <input type="text" id="slot-order" list="slot-order-list" onchange="validateDatalist(this); calcSlotEnd(); renderPatientContactBanner(this.dataset.patientId);" onblur="validateDatalist(this); calcSlotEnd(); renderPatientContactBanner(this.dataset.patientId);" placeholder="Type or select Accession (Leave blank to block time)...">
        <datalist id="slot-order-list"></datalist><br><br>

        <div style="display: flex; gap: 15px;">
            <div style="flex: 1;"><label>Start Time *</label><input type="datetime-local" id="slot-start" onchange="calcSlotEnd()" required></div>
            <div style="flex: 1;"><label>End Time *</label><input type="datetime-local" id="slot-end" required></div>
        </div><br>

        <label>Status</label>
        <select id="slot-status">
            <option value="BOOKED">BOOKED</option>
            <option value="BLOCKED_MAINTENANCE">BLOCKED_MAINTENANCE</option>
            <option value="RESERVED_STAT">RESERVED_STAT</option>
        </select><br><br>

        <button class="btn" onclick="submitSlot(false)">Save Slot</button>
        <button class="btn" style="background:#2ecc71; margin-left:6px;" onclick="submitSlot(true)">Save Slot &amp; Send Reminder</button>
        <div id="slot-error" style="color: #e74c3c; font-weight: bold; margin-top: 10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;

    try {
        const [rooms, orders] = await Promise.all([
            execSQL(`SELECT modality_id, room_name FROM modalities_and_rooms ORDER BY room_name`),
            execSQL(`
                SELECT o.order_id, o.accession_number, o.patient_id, o.modality_id, p.last_name, p.first_name, e.exam_name, e.default_duration_mins
                FROM imaging_orders o
                JOIN patients p ON o.patient_id = p.patient_id
                JOIN exam_catalog e ON o.exam_code = e.exam_code
                WHERE o.workflow_status IN ('ORDERED', 'SCHEDULED')
                ORDER BY o.ordered_at DESC
            `)
        ]);

        if (Array.isArray(rooms)) {
            document.getElementById('slot-room-list').innerHTML = rooms.map(r => `<option value="${r.room_name} (ID:${r.modality_id})" data-id="${r.modality_id}">`).join('');
        }
        if (Array.isArray(orders)) {
            document.getElementById('slot-order-list').innerHTML = orders.map(o =>
                `<option value="${o.accession_number} - ${o.last_name}, ${o.first_name} (${o.exam_name})" data-id="${o.order_id}" data-duration="${o.default_duration_mins || 30}" data-patient-id="${o.patient_id}">`
            ).join('');

            if (preselectOrderId) {
                const match = orders.find(o => String(o.order_id) === String(preselectOrderId));
                if (match) {
                    const ordIn = document.getElementById('slot-order');
                    ordIn.value = `${match.accession_number} - ${match.last_name}, ${match.first_name} (${match.exam_name})`;
                    ordIn.dataset.val = String(match.order_id);
                    ordIn.dataset.duration = String(match.default_duration_mins || 30);
                    ordIn.dataset.patientId = String(match.patient_id);
                    renderPatientContactBanner(match.patient_id);

                    if (match.modality_id && Array.isArray(rooms)) {
                        const rm = rooms.find(r => String(r.modality_id) === String(match.modality_id));
                        if (rm) {
                            const rmIn = document.getElementById('slot-room');
                            rmIn.value = `${rm.room_name} (ID:${rm.modality_id})`;
                            rmIn.dataset.val = String(rm.modality_id);
                        }
                    }
                }
            }
        }
    } catch (e) {}
}

async function submitSlot(sendReminderAfter = false) {
    const roomIn = document.getElementById('slot-room');
    const orderIn = document.getElementById('slot-order');
    validateDatalist(roomIn);
    validateDatalist(orderIn);

    const modId = roomIn.dataset.val;
    const orderId = orderIn.dataset.val || 'NULL';
    const rawStart = document.getElementById('slot-start').value;
    const rawEnd = document.getElementById('slot-end').value;

    if (!modId) return document.getElementById('slot-error').innerText = "Please select a valid Modality Room.";
    if (!rawStart || !rawEnd) return document.getElementById('slot-error').innerText = "Select start and end times.";

    const startStr = rawStart.replace('T', ' ') + ':00';
    const endStr = rawEnd.replace('T', ' ') + ':00';

    const roomOverlap = await execSQL(`
        SELECT slot_id FROM appointment_slots
        WHERE modality_id = ${modId}
          AND slot_status NOT IN ('CANCELLED', 'COMPLETED')
          AND ('${startStr}' < slot_end_time AND '${endStr}' > slot_start_time)
    `);
    if (roomOverlap && roomOverlap.length > 0) {
        return document.getElementById('slot-error').innerText = "OVERLAP CONFLICT: The selected modality room is already booked during this time window.";
    }

    let accNum = '';
    if (orderId !== 'NULL') {
        const patRes = await execSQL(`SELECT patient_id, accession_number FROM imaging_orders WHERE order_id = ${orderId}`);
        if (patRes && patRes.length > 0) {
            const patId = patRes[0].patient_id;
            accNum = patRes[0].accession_number;
            const patOverlap = await execSQL(`
                SELECT s.slot_id FROM appointment_slots s
                JOIN imaging_orders o ON s.order_id = o.order_id
                WHERE o.patient_id = ${patId}
                  AND s.slot_status NOT IN ('CANCELLED', 'COMPLETED')
                  AND ('${startStr}' < s.slot_end_time AND '${endStr}' > s.slot_start_time)
            `);
            if (patOverlap && patOverlap.length > 0) {
                return document.getElementById('slot-error').innerText = "OVERLAP CONFLICT: This patient is already booked for another procedure during this time window.";
            }
        }
    }

    const query = `INSERT INTO appointment_slots (modality_id, order_id, slot_start_time, slot_end_time, slot_status) VALUES (${modId}, ${orderId}, '${startStr}', '${endStr}', '${document.getElementById('slot-status').value}')`;
    const data = await execSQL(query);
    if (data && data.error) {
        document.getElementById('slot-error').innerText = data.error;
    } else {
        if (orderId !== 'NULL') {
            await execSQL(`UPDATE imaging_orders SET modality_id = ${modId}, scheduled_at = '${startStr}', workflow_status = 'SCHEDULED' WHERE order_id = ${orderId}`);
            if (sendReminderAfter) return sendOrderReminders(orderId, accNum);
        }
        g_apptViewMode === 'calendar' ? loadAppointmentsCalendar() : loadAppointments();
    }
}

async function editSlotUI(slotId) {
    const slotData = await execSQL(`SELECT modality_id, order_id, slot_start_time, slot_end_time, slot_status FROM appointment_slots WHERE slot_id = ${slotId}`);
    if (!slotData || slotData.error || slotData.length === 0) return;
    const s = slotData[0];
    let html = `
    <div class='header-flex'><h2>Edit Slot #${slotId}</h2><button class='btn btn-danger' onclick='g_apptViewMode==="calendar"?loadAppointmentsCalendar():loadAppointments()'>Cancel</button></div>
    <div id="patient-contact-banner"></div>
    <div style="background: #fff; padding: 20px; max-width: 650px; margin-top: 10px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <label>Room *</label><select id="es-room"></select><br><br>
        <label>Order</label><select id="es-order" onchange="renderPatientContactBanner(this.options[this.selectedIndex]?.dataset.patientId || null)"><option value="NULL">-- None --</option></select><br><br>
        <div style="display: flex; gap: 15px;">
            <div style="flex:1"><label>Start</label><input type="datetime-local" id="es-start" value="${s.slot_start_time.replace(' ', 'T').substring(0, 16)}"></div>
            <div style="flex:1"><label>End</label><input type="datetime-local" id="es-end" value="${s.slot_end_time.replace(' ', 'T').substring(0, 16)}"></div>
        </div><br>
        <label>Status</label><select id="es-status"><option value="AVAILABLE" ${s.slot_status==='AVAILABLE'?'selected':''}>AVAILABLE</option><option value="BOOKED" ${s.slot_status==='BOOKED'?'selected':''}>BOOKED</option><option value="BLOCKED_MAINTENANCE" ${s.slot_status==='BLOCKED_MAINTENANCE'?'selected':''}>BLOCKED_MAINTENANCE</option><option value="RESERVED_STAT" ${s.slot_status==='RESERVED_STAT'?'selected':''}>RESERVED_STAT</option><option value="COMPLETED" ${s.slot_status==='COMPLETED'?'selected':''}>COMPLETED</option><option value="CANCELLED" ${s.slot_status==='CANCELLED'?'selected':''}>CANCELLED</option><option value="NO_SHOW" ${s.slot_status==='NO_SHOW'?'selected':''}>NO_SHOW</option></select><br><br>
        <button class="btn" onclick="submitEditSlot(${slotId})">Update Slot</button><div id="es-error" style="color:#e74c3c; font-weight:bold; margin-top:8px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;

    const [rooms, orders] = await Promise.all([
        execSQL(`SELECT modality_id, room_name FROM modalities_and_rooms ORDER BY room_name`),
        execSQL(`SELECT o.order_id, o.accession_number, o.patient_id, p.last_name, p.first_name FROM imaging_orders o JOIN patients p ON o.patient_id = p.patient_id ORDER BY o.ordered_at DESC`)
    ]);
    if (Array.isArray(rooms)) {
        document.getElementById('es-room').innerHTML = rooms.map(r => `<option value="${r.modality_id}" ${r.modality_id==s.modality_id?'selected':''}>${r.room_name}</option>`).join('');
    }
    if (Array.isArray(orders)) {
        document.getElementById('es-order').innerHTML += orders.map(o =>
            `<option value="${o.order_id}" data-patient-id="${o.patient_id}" ${o.order_id==s.order_id?'selected':''}>${o.accession_number} - ${o.last_name}, ${o.first_name}</option>`
        ).join('');
        const linked = orders.find(o => String(o.order_id) === String(s.order_id));
        if (linked) renderPatientContactBanner(linked.patient_id);
    }
}

async function submitEditSlot(slotId) {
    const modId = document.getElementById('es-room').value;
    const orderId = document.getElementById('es-order').value;
    const startStr = document.getElementById('es-start').value.replace('T', ' ') + ':00';
    const endStr = document.getElementById('es-end').value.replace('T', ' ') + ':00';
    const status = document.getElementById('es-status').value;

    const q = `UPDATE appointment_slots SET modality_id=${modId}, order_id=${orderId}, slot_start_time='${startStr}', slot_end_time='${endStr}', slot_status='${status}' WHERE slot_id=${slotId}`;
    const data = await execSQL(q);
    if (data && data.error) {
        document.getElementById('es-error').innerText = data.error;
        return;
    }

    if (orderId !== 'NULL' && status === 'NO_SHOW') {
        await execSQL(`
            INSERT INTO workflow_state_history (order_id, previous_status, new_status, transition_notes, transition_timestamp)
            VALUES (${orderId}, 'SCHEDULED', 'NO_SHOW', 'Patient did not arrive for appointment slot #${slotId}', datetime('now'))
        `);
        await execSQL(`UPDATE imaging_orders SET scheduled_at = NULL WHERE order_id = ${orderId}`);
    }

    g_apptViewMode === 'calendar' ? loadAppointmentsCalendar() : loadAppointments();
}
