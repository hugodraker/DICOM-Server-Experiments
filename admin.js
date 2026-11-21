/* --- EXTERNAL COMMUNICATION & REPORT DISPATCH GATEWAYS --- */
const REMINDER_TOKENS = ['{to}', '{fax}', '{patient}', '{mrn}', '{exam}', '{datetime}', '{room}', '{accession}', '{doctor}', '{status}', '{payload_file}', '{pdf_file}', '{message}'];

/* --- RSNA STRUCTURED REPORTING TEMPLATES (18-COLUMN SCHEMA) --- */
const RSNA_COLS = [
    'modality', 'anatomy', 'pathology',
    'v1_label', 'v1_key', 'v1_values',
    'v2_label', 'v2_key', 'v2_values',
    'v3_label', 'v3_key', 'v3_values',
    'v4_label', 'v4_key', 'v4_values',
    'base_template', 'findings_template', 'impression_template'
];

/* --- SQL TYPST REPORT TEMPLATE EDITOR --- */
const TYPST_REPORT_TOKENS = [
    '{facility_name}','{facility_address}','{facility_phone}',
    '{patient_name}','{accession}','{mrn}','{dob}','{sex}',
    '{exam_name}','{modality}','{referring_md}','{radiologist}','{radiologist_license}',
    '{report_status}','{version}','{signed_at}',
    '{clinical_history}','{comparison}','{technique}','{findings}','{impression}','{standardized_assessment}'
];

async function adminSendPasswordReset(identifier) {
    const res = await fetch('/api/request_password_reset', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ identifier }).toString()
    });
    const data = await res.json();
    if (data.error) return setUserAdminMsg(data.error, true);
    setUserAdminMsg(`Password reset email queued for ${data.email} (status: ${data.dispatch_status}).`);
}

async function browseDBTable() {
    const tbl = document.getElementById('db-table-sel')?.value;
    const whereRaw = (document.getElementById('db-where-in')?.value || '').trim();
    const limit = parseInt(document.getElementById('db-limit-in')?.value || '100', 10) || 100;
    if (!tbl) return;

    const sql = `SELECT * FROM "${tbl}"${whereRaw ? ` WHERE ${whereRaw}` : ''} LIMIT ${limit}`;
    const rawBox = document.getElementById('db-raw-sql');
    if (rawBox) rawBox.value = sql;

    await executeAndRenderDBQuery(sql, tbl);
}

function buildRsnaAdminHeader() {
    return `
    <div style="margin-top: 30px;" class='header-flex'>
        <h3 style="color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 5px; margin:0;">RSNA Structured Reporting Templates</h3>
        <div>
            <button class='btn' style='margin:0; background:#34495e;' onclick='triggerRsnaImport()'>Import CSV</button>
            <button class='btn' style='margin:0; background:#34495e;' onclick='exportRsnaCSV()'>Export CSV</button>
            <button class='btn' style='margin:0;' onclick='newRsnaTemplateUI()'>+ Add</button>
        </div>
    </div>
    <div id="rsna-import-status" style="display:none; margin:8px 0; padding:8px 12px; border-radius:4px; font-size:12px; font-weight:bold;"></div>
    <div id="admin-rsna_reporting_templates" style="margin-bottom: 30px;">Loading...</div>
    `;
}

async function createNewUserAccount() {
    const me = await ensureCurrentUser();
    if (me.role !== 'SYSTEM_ADMIN') return setUserAdminMsg('Only SYSTEM_ADMIN can create user accounts.', true);

    const uname = (document.getElementById('nu-uname')?.value || '').trim();
    const fname = (document.getElementById('nu-fname')?.value || '').trim();
    const lname = (document.getElementById('nu-lname')?.value || '').trim();
    const cred  = (document.getElementById('nu-cred')?.value  || '').trim();
    const role  = document.getElementById('nu-role')?.value;
    const email = (document.getElementById('nu-email')?.value || '').trim();
    const fax   = (document.getElementById('nu-fax')?.value   || '').trim();
    const ch    = document.getElementById('nu-ch')?.value || 'PORTAL';
    const pass  = document.getElementById('nu-pass')?.value || '';

    if (!uname || !fname || !lname || !email || pass.length < 6) {
        return setUserAdminMsg('Username, First/Last Name, Email, and Password (min 6 chars) are required.', true);
    }

    const facRow = await execSQL(`SELECT facility_id FROM facilities ORDER BY facility_id ASC LIMIT 1`);
    const facId = (Array.isArray(facRow) && facRow[0]?.facility_id) ? facRow[0].facility_id : 'NULL';

    const ins = await execSQL(`
        INSERT INTO staff_users (
            username, password_hash, role_id, primary_facility_id,
            first_name, last_name, credentials, email, fax_number, preferred_comm_channel, is_active
        ) VALUES (
            ${sqlStr(uname)}, 'PENDING', ${role}, ${facId},
            ${sqlStr(fname)}, ${sqlStr(lname)}, ${sqlStr(cred, true)}, ${sqlStr(email)}, ${sqlStr(fax, true)}, ${sqlStr(ch)}, 1
        )
    `);
    if (ins && ins.error) return setUserAdminMsg(ins.error, true);

    const row = await execSQL(`SELECT user_id FROM staff_users WHERE username = ${sqlStr(uname)} LIMIT 1`);
    const uid = row?.[0]?.user_id;
    if (uid) {
        const pwRes = await fetch('/api/admin_set_password', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({ user_id: String(uid), password: pass }).toString()
        });
        const pwData = await pwRes.json();
        if (pwData.error) return setUserAdminMsg(`User created, but password failed: ${pwData.error}`, true);
    }
    await loadUserAccountsAdmin();
    setUserAdminMsg(`Account "${uname}" created with password.`);
}

async function deleteDBRow(tableName, pkCol, pkVal) {
    const res = await execSQL(`DELETE FROM "${tableName}" WHERE "${pkCol}" = '${pkVal}'`);
    if (res && res.error) setDBStatus(`Delete Error: ${res.error}`, true);
    else browseDBTable();
}

async function ensureCurrentUser() {
    if (!window.g_currentUser || !window.g_currentUser.user_id) {
        try {
            const res = await fetch('/api/me');
            window.g_currentUser = await res.json();
        } catch (e) {
            window.g_currentUser = { authenticated: false };
        }
    }
    return window.g_currentUser;
}

function exportCurrentDBTable() {
    const tbl = document.getElementById('db-table-sel')?.value;
    if (tbl) exportCSV(tbl);
}
async function executeAndRenderDBQuery(sql, tableName = '') {
    const wrap = document.getElementById('db-grid-wrap');
    if (!wrap) return;
    setDBStatus('');

    const data = await execSQL(sql);
    if (data && data.error) {
        setDBStatus(`SQL Error: ${data.error}`, true);
        return;
    }

    if (!Array.isArray(data)) {
        setDBStatus('Statement executed successfully.');
        if (tableName) browseDBTable();
        return;
    }

    if (data.length === 0) {
        setDBStatus('Query returned 0 rows.');
        wrap.innerHTML = '<p>No rows returned.</p>';
        return;
    }

    setDBStatus(`Returned ${data.length} row(s).`);
    const cols = Object.keys(data[0]);
    const pkCol = tableName ? cols[0] : null;

    let html = `<table style="width:100%; border-collapse:collapse; font-size:12px;"><thead><tr>`;
    cols.forEach(c => {
        html += `<th style="padding:6px; background:#2c3e50; color:#fff; text-align:left; border-bottom:2px solid #1a252f;">${c}</th>`;
    });
    if (pkCol) html += `<th style="padding:6px; background:#2c3e50; color:#fff; text-align:left;">Actions</th>`;
    html += `</tr></thead><tbody>`;

    data.forEach(row => {
        html += `<tr>`;
        cols.forEach(c => {
            const val = row[c] !== null && row[c] !== undefined ? String(row[c]) : '';
            html += `<td style="padding:5px 6px; border-bottom:1px solid #ecf0f1; max-width:260px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;" title="${val.replace(/"/g, '&quot;')}">${val.replace(/</g, '&lt;')}</td>`;
        });
        if (pkCol) {
            const pkVal = String(row[pkCol]).replace(/'/g, "''");
            html += `<td style="padding:5px 6px; border-bottom:1px solid #ecf0f1; white-space:nowrap;">
                <button class="btn btn-danger" style="padding:2px 6px; font-size:11px; margin:0;" onclick="deleteDBRow('${tableName}', '${pkCol}', '${pkVal}')">Del</button>
            </td>`;
        }
        html += `</tr>`;
    });

    wrap.innerHTML = html + `</tbody></table>`;
}

async function editRsnaTemplateUI(id) {
    const res = await execSQL(`SELECT * FROM rsna_reporting_templates WHERE template_row_id = ${id}`);
    if (!Array.isArray(res) || !res.length) return;
    renderRsnaTemplateForm(res[0]);
}

async function exportRsnaCSV() {
    const data = await execSQL(`SELECT ${RSNA_COLS.join(', ')} FROM rsna_reporting_templates ORDER BY modality, anatomy, pathology`);
    if (!Array.isArray(data) || data.length === 0) {
        return setRsnaStatus('No RSNA reporting templates available to export.', true);
    }

    const escPipe = v => (v === null || v === undefined ? '' : String(v))
        .replace(/\r\n/g, '\\n')
        .replace(/\n/g, '\\n')
        .replace(/\|/g, '/');

    let csv = RSNA_COLS.join('|') + '\n';
    data.forEach(row => {
        csv += RSNA_COLS.map(c => escPipe(row[c])).join('|') + '\n';
    });

    const blob = new Blob([csv], { type: 'text/csv' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'RSNA.csv';
    a.click();
    setRsnaStatus(`Exported ${data.length} RSNA template(s) to RSNA.csv (pipe-delimited).`);
}

async function editShiftUI(id) {
    const res = await execSQL(`SELECT * FROM staff_shift_schedules WHERE shift_id = ${id}`);
    if (!res || !res.length) return;
    const d = res[0];

    let html = `
    <div class='header-flex'><h2>Edit Shift Schedule #${id}</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 600px; margin-top: 15px;">
        <label>Shift Type *</label>
        <select id="edit-shift-type">
            <option value="WORKING_REGULAR" ${d.shift_type==='WORKING_REGULAR'?'selected':''}>WORKING_REGULAR</option>
            <option value="ON_CALL_PRIMARY" ${d.shift_type==='ON_CALL_PRIMARY'?'selected':''}>ON_CALL_PRIMARY</option>
            <option value="ON_CALL_BACKUP" ${d.shift_type==='ON_CALL_BACKUP'?'selected':''}>ON_CALL_BACKUP</option>
            <option value="OFF_SICK" ${d.shift_type==='OFF_SICK'?'selected':''}>OFF_SICK</option>
            <option value="OFF_VACATION" ${d.shift_type==='OFF_VACATION'?'selected':''}>OFF_VACATION</option>
            <option value="OFF_CME_TRAINING" ${d.shift_type==='OFF_CME_TRAINING'?'selected':''}>OFF_CME_TRAINING</option>
        </select><br><br>

        <div style="display: flex; gap: 15px;">
            <div style="flex:1"><label>Shift Start *</label><input type="datetime-local" id="edit-shift-start" value="${d.shift_start.replace(' ', 'T').substring(0,16)}" required></div>
            <div style="flex:1"><label>Shift End *</label><input type="datetime-local" id="edit-shift-end" value="${d.shift_end.replace(' ', 'T').substring(0,16)}" required></div>
        </div><br>

        <label>Notes</label>
        <input type="text" id="edit-shift-notes" value="${d.notes || ''}"><br><br>

        <button class="btn" onclick="submitEditShift(${id})">Update Shift</button>
        <div id="edit-shift-err" style="color:#e74c3c; font-weight: bold; margin-top: 10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}

/* --- IMPORT/EXPORT UTILITIES --- */
async function exportCSV(table) {
    const data = await execSQL(`SELECT * FROM ${table}`);
    if (!data || data.error || data.length === 0) return alert("No data available to export or database error.");
    
    const headers = Object.keys(data[0]);
    let csv = headers.join(',') + '\n';
    
    data.forEach(row => {
        csv += headers.map(h => {
            let val = row[h] === null ? '' : String(row[h]);
            if (val.includes(',') || val.includes('"') || val.includes('\n')) {
                val = '"' + val.replace(/"/g, '""') + '"';
            }
            return val;
        }).join(',') + '\n';
    });

    const blob = new Blob([csv], { type: 'text/csv' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${table}_export.csv`;
    a.click();
}

function insertToken(inputId, token) {
    const el = document.getElementById(inputId);
    if (!el) return;
    const start = el.selectionStart ?? el.value.length;
    const end = el.selectionEnd ?? el.value.length;
    el.value = el.value.slice(0, start) + token + el.value.slice(end);
    el.focus();
    el.setSelectionRange(start + token.length, start + token.length);
    updateCmdPreview();
}

async function loadUserAccountsAdmin() {
    const me = await ensureCurrentUser();
    const isAdmin = (me.role === 'SYSTEM_ADMIN');

    const myPwCard = `
    <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px; font-size:12px; max-width:680px;">
        <b style="color:#2c3e50;">Change My Password (${escAttr(me.user || 'current user')} &bull; ${escAttr(me.role || '')})</b>
        <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(170px, 1fr)); gap:8px; margin-top:8px;">
            <div><label style="font-weight:bold;">Current Password *</label><input type="password" id="my-cur-pw" placeholder="Current password"></div>
            <div><label style="font-weight:bold;">New Password *</label><input type="password" id="my-new-pw" placeholder="Min 6 chars"></div>
            <div><label style="font-weight:bold;">Confirm New Password *</label><input type="password" id="my-cnf-pw" placeholder="Repeat new password"></div>
        </div>
        <div style="display:flex; gap:8px; margin-top:10px; align-items:center;">
            <button class="btn" style="background:#2980b9; margin:0;" onclick="submitChangeMyPassword()">Update My Password</button>
            ${me.email ? `<button class="btn" style="background:#8e44ad; margin:0;" onclick="adminSendPasswordReset('${escAttr(me.email)}')">Send Email Reset Token</button>` : ''}
        </div>
    </div>`;

    if (!isAdmin) {
        document.getElementById('main-content').innerHTML = `
        ${renderAdminToolsBar('USERS')}
        <div class="header-flex"><h2>My Account &amp; Password</h2></div>
        <div id="usr-status-bar" style="display:none; margin:10px 0; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
        ${myPwCard}`;
        return;
    }

    const [users, roles] = await Promise.all([
        execSQL(`
            SELECT u.user_id, u.username, u.first_name, u.last_name,
                   COALESCE(u.credentials, '') AS credentials,
                   COALESCE(u.specialty, '') AS specialty,
                   COALESCE(u.email, '') AS email,
                   COALESCE(u.work_phone, '') AS work_phone,
                   COALESCE(u.fax_number, '') AS fax_number,
                   COALESCE(u.preferred_comm_channel, 'PORTAL') AS preferred_comm_channel,
                   u.role_id, u.is_active,
                   r.role_code,
                   CASE WHEN c.user_id IS NOT NULL THEN 1 ELSE 0 END AS has_password,
                   COALESCE(c.last_login_at, 'Never') AS last_login_at
            FROM staff_users u
            JOIN roles r ON u.role_id = r.role_id
            LEFT JOIN user_auth_credentials c ON u.user_id = c.user_id
            ORDER BY u.is_active DESC, r.role_code, u.last_name
        `),
        execSQL(`SELECT role_id, role_code FROM roles ORDER BY role_id`)
    ]);

    const roleList = Array.isArray(roles) ? roles : [];
    const roleOpts = roleList.map(r => `<option value="${r.role_id}">${escAttr(r.role_code)}</option>`).join('');
    const chOpts = ['PORTAL','EMAIL','FAX','SMS','HL7_EHR'].map(c => `<option value="${c}">${c}</option>`).join('');

    const rowsHtml = (Array.isArray(users) && users.length) ? users.map(u => `
        <tr style="${u.is_active ? '' : 'opacity:0.55;'}">
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">
                <b>${escAttr(u.username)}</b>${String(u.user_id) === String(me.user_id) ? ' <span style="color:#2980b9; font-size:10px;">(You)</span>' : ''}
                <div style="font-size:10px; color:#7f8c8d;">ID #${u.user_id}</div>
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">
                <b>${escAttr(u.last_name)}, ${escAttr(u.first_name)}</b>${u.credentials ? ', ' + escAttr(u.credentials) : ''}
                <div style="font-size:10px; color:#555;">${escAttr(u.specialty || '')}</div>
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">
                <span style="background:#34495e; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px;">${escAttr(u.role_code)}</span>
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">
                ${escAttr(u.email || '(no email)')}
                <div style="font-size:10px; color:#7f8c8d;">Fax: ${escAttr(u.fax_number || '-')} | ${escAttr(u.preferred_comm_channel)}</div>
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">
                <span style="color:${u.has_password ? '#27ae60' : '#e67e22'}; font-weight:bold;">${u.has_password ? 'Password Set' : 'No Password'}</span>
                <div style="font-size:10px; color:#7f8c8d;">Last login: ${escAttr(u.last_login_at)}</div>
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; white-space:nowrap;">
                <button class="btn" style="background:#2980b9; padding:3px 7px; font-size:11px; margin:1px;" onclick="openSetUserPasswordBar(${u.user_id}, '${escAttr(u.username)}')">Set Password</button>
                <button class="btn" style="background:#8e44ad; padding:3px 7px; font-size:11px; margin:1px;" onclick="adminSendPasswordReset('${escAttr(u.email || u.username)}')">Email Reset</button>
                <button class="btn" style="background:${u.is_active ? '#e67e22' : '#27ae60'}; padding:3px 7px; font-size:11px; margin:1px;" onclick="toggleUserAccountActive(${u.user_id}, ${u.is_active ? 0 : 1})">${u.is_active ? 'Disable' : 'Enable'}</button>
            </td>
        </tr>`).join('')
        : `<tr><td colspan="6" style="padding:14px;">No staff users found.</td></tr>`;

    document.getElementById('main-content').innerHTML = `
    ${renderAdminToolsBar('USERS')}
    <div class="header-flex"><h2>User Accounts &amp; Password Management</h2></div>
    <div id="usr-status-bar" style="display:none; margin:10px 0; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div id="usr-pw-bar"></div>
    ${myPwCard}

    <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px; font-size:12px;">
        <b style="color:#2c3e50;">Create New User Account (staff_users)</b>
        <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(150px, 1fr)); gap:8px; margin-top:8px;">
            <div><label style="font-weight:bold;">Username *</label><input type="text" id="nu-uname" placeholder="jdoe"></div>
            <div><label style="font-weight:bold;">First Name *</label><input type="text" id="nu-fname" placeholder="Jane"></div>
            <div><label style="font-weight:bold;">Last Name *</label><input type="text" id="nu-lname" placeholder="Doe"></div>
            <div><label style="font-weight:bold;">Credentials</label><input type="text" id="nu-cred" placeholder="MD, RT(R)"></div>
            <div><label style="font-weight:bold;">Role *</label><select id="nu-role">${roleOpts}</select></div>
            <div><label style="font-weight:bold;">Email (for Resets) *</label><input type="email" id="nu-email" placeholder="jdoe@clinic.ca"></div>
            <div><label style="font-weight:bold;">Fax / Phone</label><input type="text" id="nu-fax" placeholder="416-555-0100"></div>
            <div><label style="font-weight:bold;">Pref. Channel</label><select id="nu-ch">${chOpts}</select></div>
            <div><label style="font-weight:bold;">Initial Password *</label><input type="password" id="nu-pass" placeholder="Min 6 chars"></div>
        </div>
        <button class="btn" style="background:#27ae60; margin-top:10px;" onclick="createNewUserAccount()">+ Create User Account</button>
    </div>

    <div style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); overflow-x:auto;">
        <table style="width:100%; border-collapse:collapse;">
            <thead><tr style="background:#2c3e50; color:#fff; font-size:12px;">
                <th style="padding:6px; text-align:left;">Username</th>
                <th style="padding:6px; text-align:left;">Name</th>
                <th style="padding:6px; text-align:left;">Role</th>
                <th style="padding:6px; text-align:left;">Email / Routing</th>
                <th style="padding:6px; text-align:left;">Auth Status</th>
                <th style="padding:6px; text-align:left;">Actions</th>
            </tr></thead>
            <tbody>${rowsHtml}</tbody>
        </table>
    </div>`;
}

async function loadAdminDbEditor() {
    return loadDBEditor();
}

async function loadTypstTemplateEditor(selectedId = null) {
    await ensureCurrentUser();
    const rows = await execSQL(`SELECT * FROM typst_report_templates ORDER BY is_default DESC, modality_type, template_code`);
    const list = Array.isArray(rows) ? rows : [];
    const active = selectedId === 'NEW' ? null : (list.find(r => String(r.typst_template_id) === String(selectedId)) || list[0] || null);

    const opts = list.map(r =>
        `<option value="${r.typst_template_id}" ${active && r.typst_template_id === active.typst_template_id ? 'selected' : ''}>[${escAttr(r.modality_type)}] ${escAttr(r.template_code)}${r.is_default ? ' (Default)' : ''}</option>`
    ).join('') + `<option value="NEW" ${selectedId === 'NEW' ? 'selected' : ''}>+ Create New Template</option>`;

    const chips = TYPST_REPORT_TOKENS.map(t =>
        `<button type="button" style="padding:2px 6px; font-size:11px; margin:2px; cursor:pointer; background:#ecf0f1; border:1px solid #bdc3c7; border-radius:3px;" onclick="insertToken('tpl-typ-markup','${t}')">${t}</button>`
    ).join('');

    const mods = ['ALL','CT','MRI','XRAY','US','MAMMO','FLUORO','NM','PET','DEXA'];

    document.getElementById('main-content').innerHTML = `
    ${renderAdminToolsBar('TYPST')}
    <div class="header-flex">
        <h2>SQL Typst Report Templates</h2>
        <div style="display:flex; gap:8px;">
            <select style="width:auto; min-width:260px;" onchange="loadTypstTemplateEditor(this.value)">${opts}</select>
            <button class="btn" style="background:#8e44ad; margin:0;" onclick="previewAdminTypstTemplate()">Live Preview</button>
            <button class="btn" style="background:#27ae60; margin:0;" onclick="saveAdminTypstTemplate(${active ? active.typst_template_id : 'null'})">Save to SQL</button>
        </div>
    </div>
    <div id="tpl-typ-status" style="display:none; margin:10px 0; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>

    <div style="display:grid; grid-template-columns:1.1fr 0.9fr; gap:12px; margin-top:10px;">
        <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:12px;">
            <div style="display:grid; grid-template-columns:1fr 1.2fr 0.7fr auto; gap:8px; align-items:end; margin-bottom:10px;">
                <div><label style="font-weight:bold;">Template Code *</label><input type="text" id="tpl-typ-code" value="${escAttr(active?.template_code || '')}" placeholder="CT_STROKE_REPORT"></div>
                <div><label style="font-weight:bold;">Template Name *</label><input type="text" id="tpl-typ-name" value="${escAttr(active?.template_name || '')}" placeholder="CT Stroke Report"></div>
                <div><label style="font-weight:bold;">Modality</label>
                    <select id="tpl-typ-mod">${mods.map(m => `<option value="${m}" ${(active?.modality_type || 'ALL') === m ? 'selected' : ''}>${m}</option>`).join('')}</select>
                </div>
                <label style="display:inline-flex; align-items:center; gap:5px; font-weight:bold; padding-bottom:6px; cursor:pointer;">
                    <input type="checkbox" id="tpl-typ-def" style="width:auto;" ${active?.is_default ? 'checked' : ''}> Default
                </label>
            </div>
            <div style="margin-bottom:6px;"><b>Insert Placeholder:</b><br>${chips}</div>
            <textarea id="tpl-typ-markup" rows="24" style="font-family:monospace; font-size:12px; line-height:1.4;">${escAttr(active?.typst_markup || '')}</textarea>
        </div>
        <div id="tpl-typ-preview" style="height:560px; background:#e6e6e6; border-radius:4px; overflow:hidden; box-shadow:0 1px 3px rgba(0,0,0,0.15);"></div>
    </div>`;

    if (active?.typst_markup) previewAdminTypstTemplate();
}

/* --- SQLITE DATABASE & TABLE EDITOR --- */
async function loadDBEditor(selectedTable = '') {
    const me = await ensureCurrentUser();
    if (me.role !== 'SYSTEM_ADMIN') return loadUserAccountsAdmin();

    const tablesRes = await execSQL(`
        SELECT name, type
        FROM sqlite_master
        WHERE type IN ('table', 'view')
          AND name NOT LIKE 'sqlite_%'
          AND name NOT LIKE 'fts_%'
        ORDER BY CASE type WHEN 'table' THEN 1 ELSE 2 END, name ASC
    `);

    const items = Array.isArray(tablesRes) ? tablesRes : [];
    const activeTable = selectedTable || (items[0]?.name || '');
    const opts = items.map(t =>
        `<option value="${t.name}" ${t.name === activeTable ? 'selected' : ''}>${t.name} (${t.type})</option>`
    ).join('');

    document.getElementById('main-content').innerHTML = `
    ${renderAdminToolsBar('DB')}
    <div class='header-flex'>
        <h2>SQLite Database Editor (ris.db)</h2>
        <div style="display:flex; gap:8px; align-items:center;">
            <button class="btn" style="background:#34495e; margin:0;" onclick="exportCurrentDBTable()">Export Table CSV</button>
            <button class="btn" style="background:#3498db; margin:0;" onclick="browseDBTable()">Refresh</button>
        </div>
    </div>

    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin:12px 0;">
        <div style="min-width:240px;">
            <label style="font-size:12px; font-weight:bold;">Table / View</label>
            <select id="db-table-sel" onchange="browseDBTable()">${opts}</select>
        </div>
        <div style="flex:1; min-width:180px;">
            <label style="font-size:12px; font-weight:bold;">WHERE Filter (Optional)</label>
            <input type="text" id="db-where-in" placeholder="e.g. workflow_status = 'READY_FOR_READ'" onkeydown="if(event.key==='Enter') browseDBTable()">
        </div>
        <div style="width:100px;">
            <label style="font-size:12px; font-weight:bold;">Row Limit</label>
            <input type="number" id="db-limit-in" value="100" min="1" max="2000">
        </div>
        <div>
            <button class="btn" style="margin:0;" onclick="browseDBTable()">Load Rows</button>
        </div>
    </div>

    <details style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px;" open>
        <summary style="font-size:13px; font-weight:bold; color:#2c3e50; cursor:pointer;">Raw SQL Console</summary>
        <textarea id="db-raw-sql" rows="4" style="margin-top:8px; font-family:monospace; font-size:12px;" placeholder="SELECT * FROM imaging_orders LIMIT 25;"></textarea>
        <div style="display:flex; gap:8px; margin-top:8px;">
            <button class="btn" style="background:#27ae60; margin:0;" onclick="runRawDBSql()">Execute SQL</button>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="document.getElementById('db-raw-sql').value=''">Clear</button>
        </div>
    </details>

    <div id="db-status-msg" style="display:none; margin-bottom:10px; padding:8px 12px; border-radius:4px; font-size:12px; font-weight:bold;"></div>
    <div id="db-grid-wrap" style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); overflow-x:auto;">Loading...</div>`;

    if (activeTable) browseDBTable();
    else document.getElementById('db-grid-wrap').innerHTML = '<p>No tables found in database.</p>';
}
async function loadReminderGatewaysUI() {
    const rows = await execSQL(`SELECT setting_key, setting_value FROM app_settings WHERE setting_key LIKE 'rem_%' OR setting_key LIKE 'rep_%'`);
    const cfg = {};
    if (Array.isArray(rows)) rows.forEach(r => cfg[r.setting_key] = r.setting_value);

    const useJsTypstChecked = cfg['rep_use_js_typst'] !== '0' ? 'checked' : '';

    const buildBlock = (prefix, key, title, defExe, defArgs) => {
        const chips = REMINDER_TOKENS.map(t =>
            `<button type="button" style="padding:2px 6px; font-size:11px; margin:2px; cursor:pointer; background:#ecf0f1; border:1px solid #bdc3c7; border-radius:3px;" onclick="insertToken('cfg-${prefix}-${key}-args','${t}')">${t}</button>`
        ).join('');
        return `
        <div style="border:1px solid #ddd; border-radius:4px; padding:12px; margin-bottom:12px; background:#fafafa;">
            <h4 style="margin:0 0 8px 0; color:#2c3e50;">${title}</h4>
            <label style="font-size:12px; font-weight:bold;">Executable Path (.exe)</label>
            <input type="text" id="cfg-${prefix}-${key}-exe" value="${(cfg[`${prefix}_${key}_exe`] || defExe).replace(/"/g, '&quot;')}" oninput="updateCmdPreview()" placeholder="C:\\Tools\\gateway.exe" style="margin-bottom:8px;">
            <label style="font-size:12px; font-weight:bold;">Custom Command-Line Arguments</label>
            <div style="margin:4px 0;">${chips}</div>
            <input type="text" id="cfg-${prefix}-${key}-args" value="${(cfg[`${prefix}_${key}_args`] || defArgs).replace(/"/g, '&quot;')}" oninput="updateCmdPreview()">
            <div style="font-size:11px; color:#555; margin-top:6px; font-family:monospace; word-break:break-all;">
                <b>Preview:</b> <span id="cfg-${prefix}-${key}-preview"></span>
            </div>
        </div>`;
    };

    const el = document.getElementById('admin-reminder-gateways');
    if (!el) return;
    el.innerHTML = `
    <div class='header-flex' style="margin-top:20px; margin-bottom:10px;">
        <h3 style="color:#2c3e50; border-bottom:2px solid #3498db; padding-bottom:5px; margin:0;">Outbound Communication &amp; Report Dispatch Gateways (.exe)</h3>
        <button class='btn' style='margin:0;' onclick='saveReminderGateways()'>Save All Gateway Configs</button>
    </div>
    <div style="background:#fff; padding:15px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <h4 style="margin:0 0 10px 0; color:#1b4f72;">1. Patient Appointment Reminder Gateways</h4>
        ${buildBlock('rem', 'email', 'Email Sender Executable (EMAIL)', '', '--to "{to}" --subject "Appointment Reminder: {exam}" --body "{message}"')}
        ${buildBlock('rem', 'sms', 'SMS Gateway Executable (SMS)', '', '--phone "{to}" --text "{message}"')}
        ${buildBlock('rem', 'tts', 'Automated Phone TTS Executable (PHONE_TTS)', '', '--dial "{to}" --say "{message}"')}
        <div style="margin-bottom:16px;">
            <label style="font-size:12px; font-weight:bold;">Default Reminder Message Template</label>
            <input type="text" id="cfg-msg-template" value="${(cfg['rem_msg_template'] || 'Hello {patient}, reminder for your {exam} on {datetime} in {room} (Ref: {accession}).').replace(/"/g, '&quot;')}">
        </div>

        <h4 style="margin:16px 0 10px 0; color:#1b4f72; border-top:2px solid #ecf0f1; padding-top:12px;">2. Typst PDF Renderer &amp; Outbound Report Dispatch Gateways</h4>
        <div style="border:1px solid #aed6f1; border-radius:4px; padding:12px; margin-bottom:12px; background:#eef5fb;">
            <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; margin-bottom:8px;">
                <h4 style="margin:0; color:#1b4f72;">Typst PDF Renderer (JS Engine / typst.exe &rarr; {pdf_file})</h4>
                <label style="font-size:12px; font-weight:bold; color:#1b4f72; display:inline-flex; align-items:center; gap:6px; cursor:pointer; background:#fff; padding:4px 10px; border:1px solid #aed6f1; border-radius:4px;">
                    <input type="checkbox" id="cfg-rep-use-js-typst" style="width:auto; margin:0;" ${useJsTypstChecked}>
                    Use JS Typst
                </label>
            </div>
            <label style="font-size:12px; font-weight:bold;">Fallback typst.exe Path (invoked when Use JS Typst is unchecked: <code>"typst.exe" "report.typ" "report.pdf"</code>)</label>
            <input type="text" id="cfg-rep-typst-exe" value="${(cfg['rep_typst_exe'] || '').replace(/"/g, '&quot;')}" placeholder="C:\\Tools\\typst.exe">
        </div>

        ${buildBlock('rep', 'hl7', 'HL7 ORU^R01 / ORM Engine Executable (HL7_ORU_R01)', '', '--host "{to}" --acc "{accession}" --file "{payload_file}" --pdf "{pdf_file}"')}
        ${buildBlock('rep', 'fax', 'Outbound Fax Server Executable (FAX)', '', '--fax "{fax}" --doctor "{doctor}" --acc "{accession}" --pdf "{pdf_file}"')}
        ${buildBlock('rep', 'fhir', 'FHIR Push Gateway Executable (HL7_FHIR_PUSH)', '', '--endpoint "{to}" --mrn "{mrn}" --acc "{accession}" --file "{payload_file}" --pdf "{pdf_file}"')}
        ${buildBlock('rep', 'portal', 'Patient Portal Notification Executable (PATIENT_PORTAL)', '', '--mrn "{mrn}" --to "{to}" --acc "{accession}" --pdf "{pdf_file}" --body "{message}"')}
        <div id="rem-cfg-status" style="color:#27ae60; font-weight:bold; margin-top:8px;"></div>
    </div>`;
    updateCmdPreview();
}

function newRsnaTemplateUI() {
    renderRsnaTemplateForm(null);
}

/* --- STAFF SHIFT & ROOM DELEGATION ADMIN --- */

// 1. Add the Shift Administration section to loadAdmin()
// (Include this line inside your main loadAdmin function HTML template)
/*
    html += buildAdminHeader("Staff Shifts & Room Delegation", "staff_shift_schedules", "newShiftUI");
*/
// And fetch the data inside loadAdmin():
/*
    const shifts = await execSQL(`
        SELECT s.shift_id, u.first_name || ' ' || u.last_name AS staff_name, f.facility_code, 
               m.room_name, s.shift_type, s.shift_start, s.shift_end, s.duration_hours 
        FROM staff_shift_schedules s 
        JOIN staff_users u ON s.user_id = u.user_id 
        JOIN facilities f ON s.facility_id = f.facility_id 
        LEFT JOIN modalities_and_rooms m ON s.assigned_modality_id = m.modality_id 
        ORDER BY s.shift_start DESC LIMIT 100
    `);
    document.getElementById('admin-staff_shift_schedules').innerHTML = buildTable(shifts, 'staff_shift_schedules', 'shift_id', 'editShiftUI');
*/

function newShiftUI() {
    let html = `
    <div class='header-flex'><h2>Delegate Room / Assign Staff Shift</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 600px; margin-top: 15px;">
        <label>Staff Member *</label><br>
        <input type="text" id="shift-user" list="shift-user-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Filter Staff..." required>
        <datalist id="shift-user-list"></datalist><br><br>

        <label>Facility *</label><br>
        <select id="shift-facility" required></select><br><br>

        <label>Assigned Modality Room (Delegation)</label><br>
        <input type="text" id="shift-room" list="shift-room-list" onchange="validateDatalist(this)" onblur="validateDatalist(this)" placeholder="Filter Rooms (Optional)...">
        <datalist id="shift-room-list"></datalist><br><br>

        <label>Shift Type *</label>
        <select id="shift-type">
            <option value="WORKING_REGULAR">WORKING_REGULAR</option>
            <option value="ON_CALL_PRIMARY">ON_CALL_PRIMARY</option>
            <option value="ON_CALL_BACKUP">ON_CALL_BACKUP</option>
            <option value="OFF_SICK">OFF_SICK</option>
            <option value="OFF_VACATION">OFF_VACATION</option>
            <option value="OFF_CME_TRAINING">OFF_CME_TRAINING</option>
        </select><br><br>

        <div style="display: flex; gap: 15px;">
            <div style="flex:1"><label>Shift Start *</label><input type="datetime-local" id="shift-start" required></div>
            <div style="flex:1"><label>Shift End *</label><input type="datetime-local" id="shift-end" required></div>
        </div><br>

        <label>Notes / Delegation Details</label>
        <input type="text" id="shift-notes" placeholder="e.g., Primary CT Console operator"><br><br>

        <button class="btn" onclick="submitShift()">Save Shift Schedule</button>
        <div id="shift-err" style="color:#e74c3c; font-weight: bold; margin-top: 10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;

    // Populate Datalists & Selectors
    execSQL(`SELECT user_id, first_name, last_name FROM staff_users WHERE is_active = 1`).then(users => {
        if (users && !users.error) document.getElementById('shift-user-list').innerHTML = users.map(u => `<option value="${u.last_name}, ${u.first_name} (ID:${u.user_id})" data-id="${u.user_id}">`).join('');
    });
    execSQL(`SELECT facility_id, facility_name FROM facilities WHERE is_active = 1`).then(facs => {
        if (facs && !facs.error) document.getElementById('shift-facility').innerHTML = facs.map(f => `<option value="${f.facility_id}">${f.facility_name}</option>`).join('');
    });
    execSQL(`SELECT modality_id, room_name FROM modalities_and_rooms WHERE operational_status = 'ONLINE'`).then(rooms => {
        if (rooms && !rooms.error) document.getElementById('shift-room-list').innerHTML = rooms.map(r => `<option value="${r.room_name} (ID:${r.modality_id})" data-id="${r.modality_id}">`).join('');
    });
}

function openSetUserPasswordBar(userId, username) {
    const bar = document.getElementById('usr-pw-bar');
    if (!bar) return;
    bar.innerHTML = `
    <div style="background:#eef5fb; border:1px solid #aed6f1; padding:10px 12px; border-radius:4px; margin-bottom:10px; display:flex; gap:8px; align-items:center; flex-wrap:wrap; font-size:12px;">
        <b>Set Password for ${escAttr(username)}:</b>
        <input type="password" id="usr-new-pw" placeholder="New password (min 6 chars)" style="width:220px;">
        <button class="btn" style="background:#27ae60; margin:0;" onclick="submitAdminSetPassword(${userId})">Save Password</button>
        <button class="btn" style="background:#7f8c8d; margin:0;" onclick="document.getElementById('usr-pw-bar').innerHTML=''">Cancel</button>
    </div>`;
}

function previewAdminTypstTemplate() {
    if (!window.TypstJS) return;
    let src = document.getElementById('tpl-typ-markup')?.value || '';
    const sample = {
        '{facility_name}': 'Metro Diagnostic Imaging Center',
        '{facility_address}': '550 Universal Ave, New York, NY',
        '{facility_phone}': '205-555-0199',
        '{patient_name}': 'Vance, Elena',
        '{accession}': 'ACC-20261120-0003',
        '{mrn}': 'MRN-100501',
        '{dob}': '1978-04-12',
        '{sex}': 'F',
        '{exam_name}': 'MRI Brain without Contrast',
        '{modality}': 'MRI',
        '{referring_md}': 'Arthur Pendelton',
        '{radiologist}': 'Sarah Jenkins, MD',
        '{radiologist_license}': 'NPI-1234567890',
        '{report_status}': 'FINAL',
        '{version}': '1',
        '{signed_at}': '2026-11-20 14:30:00',
        '{clinical_history}': 'Acute onset left-sided hemiparesis x 45 mins.',
        '{comparison}': 'None available.',
        '{technique}': 'Multiplanar MRI Brain without intravenous contrast.',
        '{findings}': 'Focus of restricted diffusion on DWI/ADC in the Right MCA territory without hemorrhagic transformation.',
        '{impression}': '1. Acute Right MCA territory ischemic infarction.\n2. No significant mass effect.',
        '{standardized_assessment}': ''
    };
    Object.entries(sample).forEach(([k, v]) => { src = src.split(k).join(v); });
    TypstJS.mountViewer('tpl-typ-preview', src, { filename: 'template_preview.pdf' });
}
async function processRsnaImport(file) {
    if (!file) return;
    const text = await file.text();
    const lines = text.split(/\r?\n/).filter(l => l.trim() !== '');
    if (lines.length < 2) return setRsnaStatus('CSV file is empty or missing data rows.', true);

    const isPipe = lines[0].includes('|') || (lines[1] && lines[1].split('|').length >= 18);
    let inserted = 0, errors = 0;

    setRsnaStatus(`Importing ${lines.length - 1} row(s)...`);

    for (let i = 1; i < lines.length; i++) {
        const vals = isPipe ? lines[i].split('|') : parseCommaCsvLine(lines[i]);
        if (vals.length < 18) { errors++; continue; }

        // If a comma-CSV export included template_row_id as col 0 (19 cols), skip it
        const rowVals = vals.length >= 19 && !isPipe ? vals.slice(1, 19) : vals.slice(0, 18);
        const sqlVals = rowVals.map(v => `'${String(v ?? '').trim().replace(/'/g, "''")}'`).join(', ');

        const res = await execSQL(`
            INSERT INTO rsna_reporting_templates (${RSNA_COLS.join(', ')})
            VALUES (${sqlVals})
        `);
        if (res && res.error) errors++; else inserted++;
    }

    await loadAdmin();
    setRsnaStatus(`RSNA Import Complete: ${inserted} row(s) imported, ${errors} error(s).`, errors > 0);
}

function parseCommaCsvLine(str) {
    const ret = [];
    let cur = '', inQ = false;
    for (let i = 0; i < str.length; i++) {
        if (str[i] === '"') {
            if (inQ && str[i + 1] === '"') { cur += '"'; i++; }
            else inQ = !inQ;
        } else if (str[i] === ',' && !inQ) {
            ret.push(cur.trim());
            cur = '';
        } else {
            cur += str[i];
        }
    }
    ret.push(cur.trim());
    return ret;
}

function renderAdminToolsBar(activeTab = 'SETTINGS') {
    const isAdmin = (window.g_currentUser?.role === 'SYSTEM_ADMIN');
    const tabBtn = (id, lbl, fn) => {
        const active = activeTab === id;
        return `<button class="btn" style="margin:0; background:${active ? '#2c3e50' : '#7f8c8d'}; font-weight:${active ? 'bold' : 'normal'};" onclick="${fn}">${lbl}</button>`;
    };
    return `
    <div style="display:flex; flex-wrap:wrap; gap:8px; background:#fff; padding:10px 12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.08); margin-bottom:12px;">
        ${tabBtn('SETTINGS', 'System Settings', 'loadAdmin()')}
        ${tabBtn('USERS',    isAdmin ? 'User Accounts' : 'My Password', 'loadUserAccountsAdmin()')}
        ${tabBtn('TYPST',    'Typst Templates', 'loadTypstTemplateEditor()')}
        ${isAdmin ? tabBtn('DB', 'Database Editor', 'loadDBEditor()') : ''}
    </div>`;
}

async function runRawDBSql() {
    const sql = (document.getElementById('db-raw-sql')?.value || '').trim();
    if (!sql) return;
    await executeAndRenderDBQuery(sql, '');
}

function renderRsnaTemplateForm(d) {
    const isEdit = !!d;
    const v = k => (d && d[k] !== null && d[k] !== undefined) ? escAttr(d[k]) : '';
    const grid3 = 'display:grid; grid-template-columns:repeat(3, 1fr); gap:10px; margin-bottom:10px;';

    let varRows = '';
    for (let i = 1; i <= 4; i++) {
        varRows += `
        <div style="${grid3}">
            <div><label>Var ${i} Label</label><input type="text" id="rsna-v${i}l" value="${v(`v${i}_label`)}" placeholder="e.g. Laterality"></div>
            <div><label>Var ${i} Key</label><input type="text" id="rsna-v${i}k" value="${v(`v${i}_key`)}" placeholder="e.g. [SIDE]"></div>
            <div><label>Var ${i} Values (;-separated)</label><input type="text" id="rsna-v${i}v" value="${v(`v${i}_values`)}" placeholder="Left;Right;Bilateral"></div>
        </div>`;
    }

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>${isEdit ? `Edit RSNA Template #${d.template_row_id}` : 'Add RSNA Reporting Template'}</h2>
        <button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button>
    </div>
    <div style="background:#fff; padding:20px; max-width:900px; margin-top:15px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <div style="${grid3}">
            <div><label>Modality *</label><input type="text" id="rsna-mod" value="${v('modality')}" placeholder="CT, MRI, XRAY, US..."></div>
            <div><label>Anatomy *</label><input type="text" id="rsna-anat" value="${v('anatomy')}" placeholder="Chest, Brain, Abdomen..."></div>
            <div><label>Pathology *</label><input type="text" id="rsna-path" value="${v('pathology')}" placeholder="Normal, Pulmonary Embolism..."></div>
        </div>
        ${varRows}
        <label>Base Template * (use [FINDINGS_INSERT] and [IMPRESSION_INSERT])</label>
        <textarea id="rsna-base" rows="4" style="margin-bottom:10px;">${v('base_template')}</textarea>
        <label>Findings Template *</label>
        <textarea id="rsna-find" rows="4" style="margin-bottom:10px;">${v('findings_template')}</textarea>
        <label>Impression Template *</label>
        <textarea id="rsna-imp" rows="3" style="margin-bottom:12px;">${v('impression_template')}</textarea>

        <button class="btn" onclick="submitRsnaTemplate(${isEdit ? d.template_row_id : 'null'})">${isEdit ? 'Update Template' : 'Save Template'}</button>
        <div id="rsna-err" style="color:#e74c3c; font-weight:bold; margin-top:8px;"></div>
    </div>`;
}

async function submitChangeMyPassword() {
    const cur = document.getElementById('my-cur-pw')?.value || '';
    const nw  = document.getElementById('my-new-pw')?.value || '';
    const cnf = document.getElementById('my-cnf-pw')?.value || '';

    if (nw.length < 6) return setUserAdminMsg('New password must be at least 6 characters.', true);
    if (nw !== cnf) return setUserAdminMsg('New password and confirmation do not match.', true);

    const res = await fetch('/api/change_my_password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ current_password: cur, new_password: nw }).toString()
    });
    const data = await res.json();
    if (data.error) return setUserAdminMsg(data.error, true);

    ['my-cur-pw', 'my-new-pw', 'my-cnf-pw'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.value = '';
    });
    setUserAdminMsg('Your password has been updated.');
}

async function submitAdminSetPassword(userId) {
    const pw = document.getElementById('usr-new-pw')?.value || '';
    if (pw.length < 6) return setUserAdminMsg('Password must be at least 6 characters.', true);
    const res = await fetch('/api/admin_set_password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ user_id: String(userId), password: pw }).toString()
    });
    const data = await res.json();
    if (data.error) return setUserAdminMsg(data.error, true);
    await loadUserAccountsAdmin();
    setUserAdminMsg(`Password updated for user #${userId}.`);
}

function setUserAdminMsg(msg, isErr = false) {
    const el = document.getElementById('usr-status-bar');
    if (!el) return;
    el.style.display = 'block';
    el.style.background = isErr ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isErr ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isErr ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

async function saveAdminTypstTemplate(id) {
    const code = (document.getElementById('tpl-typ-code')?.value || '').trim();
    const name = (document.getElementById('tpl-typ-name')?.value || '').trim();
    const mod  = document.getElementById('tpl-typ-mod')?.value || 'ALL';
    const isDef = document.getElementById('tpl-typ-def')?.checked ? 1 : 0;
    const markup = document.getElementById('tpl-typ-markup')?.value || '';
    const st = document.getElementById('tpl-typ-status');
    const showMsg = (m, err) => {
        if (!st) return;
        st.style.display = 'block';
        st.style.background = err ? '#fdedec' : '#eafaf1';
        st.style.color = err ? '#922b21' : '#1e8449';
        st.innerText = m;
    };
    if (!code || !name || !markup.trim()) return showMsg('Code, Name, and Typst Markup are required.', true);

    if (isDef) await execSQL(`UPDATE typst_report_templates SET is_default = 0`);

    const sql = id
        ? `UPDATE typst_report_templates SET template_code=${sqlStr(code)}, template_name=${sqlStr(name)}, modality_type=${sqlStr(mod)}, is_default=${isDef}, typst_markup=${sqlStr(markup)}, updated_at=datetime('now') WHERE typst_template_id=${id}`
        : `INSERT INTO typst_report_templates (template_code, template_name, modality_type, is_default, typst_markup) VALUES (${sqlStr(code)}, ${sqlStr(name)}, ${sqlStr(mod)}, ${isDef}, ${sqlStr(markup)})`;

    const res = await execSQL(sql);
    if (res && res.error) return showMsg(res.error, true);
    showMsg(`Template "${code}" saved to SQLite.`, false);
    if (!id) loadTypstTemplateEditor();
}

function setDBStatus(msg, isError = false) {
    const el = document.getElementById('db-status-msg');
    if (!el) return;
    el.style.display = msg ? 'block' : 'none';
    el.style.background = isError ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isError ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isError ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

async function submitRsnaTemplate(id) {
    const val = elId => (document.getElementById(elId)?.value || '').trim();
    const esc = s => `'${s.replace(/'/g, "''")}'`;

    const mod = val('rsna-mod'), anat = val('rsna-anat'), path = val('rsna-path');
    const base = val('rsna-base'), find = val('rsna-find'), imp = val('rsna-imp');
    if (!mod || !anat || !path || !base || !find || !imp) {
        return document.getElementById('rsna-err').innerText = 'Modality, Anatomy, Pathology, Base, Findings, and Impression are required.';
    }

    const fieldMap = {
        modality: esc(mod), anatomy: esc(anat), pathology: esc(path),
        v1_label: esc(val('rsna-v1l')), v1_key: esc(val('rsna-v1k')), v1_values: esc(val('rsna-v1v')),
        v2_label: esc(val('rsna-v2l')), v2_key: esc(val('rsna-v2k')), v2_values: esc(val('rsna-v2v')),
        v3_label: esc(val('rsna-v3l')), v3_key: esc(val('rsna-v3k')), v3_values: esc(val('rsna-v3v')),
        v4_label: esc(val('rsna-v4l')), v4_key: esc(val('rsna-v4k')), v4_values: esc(val('rsna-v4v')),
        base_template: esc(base), findings_template: esc(find), impression_template: esc(imp)
    };

    const cols = Object.keys(fieldMap);
    const q = id
        ? `UPDATE rsna_reporting_templates SET ${cols.map(c => `${c}=${fieldMap[c]}`).join(', ')} WHERE template_row_id=${id}`
        : `INSERT INTO rsna_reporting_templates (${cols.join(', ')}) VALUES (${cols.map(c => fieldMap[c]).join(', ')})`;

    const res = await execSQL(q);
    if (res && res.error) document.getElementById('rsna-err').innerText = res.error;
    else loadAdmin();
}
function setRsnaStatus(msg, isError = false) {
    const el = document.getElementById('rsna-import-status');
    if (!el) return;
    el.style.display = msg ? 'block' : 'none';
    el.style.background = isError ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isError ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isError ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

async function saveReminderGateways() {
    const useJsTypst = document.getElementById('cfg-rep-use-js-typst')?.checked ? '1' : '0';
    const pairs = [
        ['rem_email_exe',    document.getElementById('cfg-rem-email-exe').value.trim()],
        ['rem_email_args',   document.getElementById('cfg-rem-email-args').value.trim()],
        ['rem_sms_exe',      document.getElementById('cfg-rem-sms-exe').value.trim()],
        ['rem_sms_args',     document.getElementById('cfg-rem-sms-args').value.trim()],
        ['rem_tts_exe',      document.getElementById('cfg-rem-tts-exe').value.trim()],
        ['rem_tts_args',     document.getElementById('cfg-rem-tts-args').value.trim()],
        ['rem_msg_template', document.getElementById('cfg-msg-template').value.trim()],
        ['rep_use_js_typst', useJsTypst],
        ['rep_typst_exe',    document.getElementById('cfg-rep-typst-exe').value.trim()],
        ['rep_hl7_exe',      document.getElementById('cfg-rep-hl7-exe').value.trim()],
        ['rep_hl7_args',     document.getElementById('cfg-rep-hl7-args').value.trim()],
        ['rep_fax_exe',      document.getElementById('cfg-rep-fax-exe').value.trim()],
        ['rep_fax_args',     document.getElementById('cfg-rep-fax-args').value.trim()],
        ['rep_fhir_exe',     document.getElementById('cfg-rep-fhir-exe').value.trim()],
        ['rep_fhir_args',    document.getElementById('cfg-rep-fhir-args').value.trim()],
        ['rep_portal_exe',   document.getElementById('cfg-rep-portal-exe').value.trim()],
        ['rep_portal_args',  document.getElementById('cfg-rep-portal-args').value.trim()]
    ];
    for (const [k, v] of pairs) {
        await execSQL(`INSERT OR REPLACE INTO app_settings (setting_key, setting_value) VALUES ('${k}', '${v.replace(/'/g, "''")}')`);
    }
    const st = document.getElementById('rem-cfg-status');
    if (st) st.innerText = 'All reminder, Typst PDF, and outbound report gateway settings saved.';
}
async function submitEditShift(id) {
    const type = document.getElementById('edit-shift-type').value;
    const start = document.getElementById('edit-shift-start').value;
    const end = document.getElementById('edit-shift-end').value;
    const notes = document.getElementById('edit-shift-notes').value.replace(/'/g, "''");

    if (!start || !end) return document.getElementById('edit-shift-err').innerText = "Times are required.";

    const q = `
        UPDATE staff_shift_schedules 
        SET shift_type='${type}', shift_start='${start.replace('T',' ')+':00'}', shift_end='${end.replace('T',' ')+':00'}', notes='${notes}' 
        WHERE shift_id=${id}
    `;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('edit-shift-err').innerText = data.error;
    else loadAdmin();
}
async function submitShift() {
    const userIn = document.getElementById('shift-user');
    const roomIn = document.getElementById('shift-room');
    validateDatalist(userIn);
    if (roomIn.value) validateDatalist(roomIn);

    const userId = userIn.dataset.val;
    const facId = document.getElementById('shift-facility').value;
    const modId = roomIn.dataset.val || 'NULL';
    const type = document.getElementById('shift-type').value;
    const start = document.getElementById('shift-start').value;
    const end = document.getElementById('shift-end').value;
    const notes = document.getElementById('shift-notes').value.replace(/'/g, "''");

    if (!userId || !start || !end) {
        return document.getElementById('shift-err').innerText = "Staff member and shift start/end times are required.";
    }

    const startStr = start.replace('T', ' ') + ':00';
    const endStr = end.replace('T', ' ') + ':00';

    const q = `
        INSERT INTO staff_shift_schedules (user_id, facility_id, assigned_modality_id, shift_type, shift_start, shift_end, notes) 
        VALUES (${userId}, ${facId}, ${modId}, '${type}', '${startStr}', '${endStr}', '${notes}')
    `;
    const data = await execSQL(q);
    if (data && data.error) {
        document.getElementById('shift-err').innerText = data.error;
    } else {
        loadAdmin();
    }
}

async function toggleUserAccountActive(userId, nextActive) {
    const me = await ensureCurrentUser();
    if (me.role !== 'SYSTEM_ADMIN') return setUserAdminMsg('Only SYSTEM_ADMIN can modify accounts.', true);
    if (String(userId) === String(me.user_id) && nextActive === 0) {
        return setUserAdminMsg('You cannot disable your own currently active admin account.', true);
    }
    await execSQL(`UPDATE staff_users SET is_active = ${nextActive} WHERE user_id = ${userId}`);
    await loadUserAccountsAdmin();
    setUserAdminMsg(`User #${userId} ${nextActive ? 'enabled' : 'disabled'}.`);
}
function triggerRsnaImport() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.csv,.txt';
    input.onchange = e => processRsnaImport(e.target.files[0]);
    input.click();
}

function triggerRsnaPipeImport() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.csv,.txt';
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        const lines = (await file.text()).split(/\r?\n/).filter(l => l.trim() !== '');
        let ok = 0, err = 0;
        for (let i = 1; i < lines.length; i++) {
            const f = lines[i].split('|');
            if (f.length < 18) continue;
            const esc = s => `'${(s || '').trim().replace(/'/g, "''")}'`;
            const res = await execSQL(`
                INSERT INTO rsna_reporting_templates
                (modality, anatomy, pathology, v1_label, v1_key, v1_values, v2_label, v2_key, v2_values,
                 v3_label, v3_key, v3_values, v4_label, v4_key, v4_values, base_template, findings_template, impression_template)
                VALUES (${f.slice(0, 18).map(esc).join(', ')})
            `);
            if (res && res.error) err++; else ok++;
        }
        alert(`RSNA Import Complete: ${ok} inserted, ${err} errors.`);
        loadAdmin();
    };
    input.click();
}
function triggerImport(table) {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.csv';
    input.onchange = e => processImport(e.target.files[0], table);
    input.click();
}

async function processImport(file, table) {
    if (!file) return;
    const text = await file.text();
    const lines = text.split('\n').filter(l => l.trim() !== '');
    if (lines.length < 2) return alert("CSV is empty or missing data.");

    const parseLine = (str) => {
        let ret = [], cur = '', inQ = false;
        for (let i = 0; i < str.length; i++) {
            if (str[i] === '"') inQ = !inQ;
            else if (str[i] === ',' && !inQ) { ret.push(cur.trim().replace(/^"|"$/g, '')); cur = ''; }
            else cur += str[i];
        }
        ret.push(cur.trim().replace(/^"|"$/g, ''));
        return ret;
    };

    const headers = parseLine(lines[0]);
    let errors = 0;

    for (let i = 1; i < lines.length; i++) {
        const vals = parseLine(lines[i]);
        if (vals.length !== headers.length) continue;
        const safeVals = vals.map(v => v.replace(/'/g, "''"));
        const query = `INSERT OR REPLACE INTO ${table} (${headers.join(',')}) VALUES ('${safeVals.join("','")}')`;
        const res = await execSQL(query);
        if (res && res.error) errors++;
    }
    alert(`Import Complete. Encountered ${errors} row errors.`);
    loadAdmin();
}

function buildAdminHeader(title, table, createFunc) {
    return `
    <div style="margin-top: 30px;" class='header-flex'>
        <h3 style="color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 5px; margin:0;">${title}</h3>
        <div>
            <button class='btn' style='margin:0; background:#34495e;' onclick='triggerImport("${table}")'>Import CSV</button>
            <button class='btn' style='margin:0; background:#34495e;' onclick='exportCSV("${table}")'>Export CSV</button>
            <button class='btn' style='margin:0;' onclick='${createFunc}()'>+ Add</button>
        </div>
    </div>
    <div id="admin-${table}" style="margin-bottom: 30px;">Loading...</div>
    `;
}

async function loadAdmin() {
    await ensureCurrentUser();
    let html = `${renderAdminToolsBar('SETTINGS')}
                <div class='header-flex'><h2>Administration &amp; Master Files</h2></div>
                <p style="color: #777;">Manage reminder gateways, slots, referring physicians, exams, RSNA reporting templates, facilities, equipment, and clinical indications.</p>`;

    html += `<div id="admin-reminder-gateways" style="margin-bottom: 30px;">Loading Reminder Gateways...</div>`;
    html += buildRsnaAdminHeader();
    html += buildAdminHeader("Appointment Slots (Availability & Blocks)", "appointment_slots", "newAdminSlotUI");
    html += buildAdminHeader("Referring Physicians", "staff_users", "newReferringMdUI");
    html += buildAdminHeader("Procedure Exam Catalog", "exam_catalog", "newExamUI");
    html += buildAdminHeader("Facilities & Addresses", "facilities", "newFacilityUI");
    html += buildAdminHeader("Modalities & Rooms", "modalities_and_rooms", "newModalityUI");
    html += buildAdminHeader("Clinical Indications (ICD-10)", "icd10_diagnoses", "newIcd10UI");

    document.getElementById('main-content').innerHTML = html;

    await loadReminderGatewaysUI();

    const rsna = await execSQL(`
        SELECT template_row_id, modality, anatomy, pathology,
               v1_label, v2_label, v3_label, v4_label
        FROM rsna_reporting_templates
        ORDER BY modality, anatomy, pathology
        LIMIT 400
    `);
    document.getElementById('admin-rsna_reporting_templates').innerHTML =
        buildTable(rsna, 'rsna_reporting_templates', 'template_row_id', 'editRsnaTemplateUI');

    const slots = await execSQL(`SELECT s.slot_id, m.room_name, s.slot_start_time, s.slot_end_time, s.slot_status FROM appointment_slots s LEFT JOIN modalities_and_rooms m ON s.modality_id = m.modality_id ORDER BY s.slot_start_time DESC LIMIT 100`);
    document.getElementById('admin-appointment_slots').innerHTML = buildTable(slots, 'appointment_slots', 'slot_id', 'editAdminSlotUI');

    const mds = await execSQL(`SELECT user_id, npi_or_license, first_name, last_name, specialty, work_phone, email, fax_number, preferred_comm_channel FROM staff_users WHERE role_id = 6 ORDER BY last_name`);
    document.getElementById('admin-staff_users').innerHTML = buildTable(mds, 'staff_users', 'user_id', 'editReferringMdUI');

    const exams = await execSQL(`SELECT exam_code, exam_name, modality_type, body_part, requires_contrast, default_duration_mins FROM exam_catalog ORDER BY exam_code`);
    document.getElementById('admin-exam_catalog').innerHTML = buildTable(exams, 'exam_catalog', 'exam_code', 'editExamUI');

    const facs = await execSQL(`SELECT facility_id, facility_code, facility_name, address, phone FROM facilities ORDER BY facility_id`);
    document.getElementById('admin-facilities').innerHTML = buildTable(facs, 'facilities', 'facility_id', 'editFacilityUI');

    const rooms = await execSQL(`SELECT modality_id, room_name, modality_type, ae_title, operational_status FROM modalities_and_rooms ORDER BY modality_id DESC`);
    document.getElementById('admin-modalities_and_rooms').innerHTML = buildTable(rooms, 'modalities_and_rooms', 'modality_id', 'editModalityUI');

    const icd = await execSQL(`SELECT icd10_code, short_description, full_description FROM icd10_diagnoses ORDER BY icd10_code`);
    document.getElementById('admin-icd10_diagnoses').innerHTML = buildTable(icd, 'icd10_diagnoses', 'icd10_code', 'editIcd10UI');
}

/* --- ADMIN APPOINTMENT SLOT AVAILABILITY --- */
async function newAdminSlotUI() {
    let html = `
    <div class='header-flex'><h2>Add Availability Block / Slot</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 600px; margin-top: 15px;">
        <label>Modality Room *</label><br>
        <input type="text" onkeyup="filterSelect(this, 'admin-slot-room')" placeholder="Filter Rooms..." style="margin-bottom: 5px;">
        <select id="admin-slot-room" required></select><br><br>
        
        <div style="display: flex; gap: 15px;">
            <div style="flex:1"><label>Start Time *</label><input type="datetime-local" id="admin-slot-start" required></div>
            <div style="flex:1"><label>End Time *</label><input type="datetime-local" id="admin-slot-end" required></div>
        </div><br>
        
        <label>Status</label>
        <select id="admin-slot-status">
            <option value="AVAILABLE">AVAILABLE (Open for booking)</option>
            <option value="BLOCKED_MAINTENANCE">BLOCKED_MAINTENANCE</option>
        </select><br><br>
        
        <button class="btn" onclick="submitAdminSlot()">Save Slot</button>
        <div id="admin-slot-err" style="color:#e74c3c; font-weight: bold; margin-top: 10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
    const rooms = await execSQL(`SELECT modality_id, room_name FROM modalities_and_rooms ORDER BY room_name`);
    if (rooms && !rooms.error) document.getElementById('admin-slot-room').innerHTML = rooms.map(r => `<option value="${r.modality_id}">${r.room_name}</option>`).join('');
}

async function submitAdminSlot() {
    const modId = document.getElementById('admin-slot-room').value;
    const start = document.getElementById('admin-slot-start').value;
    const end = document.getElementById('admin-slot-end').value;
    const status = document.getElementById('admin-slot-status').value;
    if (!start || !end) return document.getElementById('admin-slot-err').innerText = "Times required.";
    const q = `INSERT INTO appointment_slots (modality_id, slot_start_time, slot_end_time, slot_status) VALUES (${modId}, '${start.replace('T',' ')+':00'}', '${end.replace('T',' ')+':00'}', '${status}')`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('admin-slot-err').innerText = data.error; else loadAdmin();
}

async function editAdminSlotUI(id) {
    const res = await execSQL(`SELECT * FROM appointment_slots WHERE slot_id = ${id}`);
    if (!res || !res.length) return; const d = res[0];
    let html = `
    <div class='header-flex'><h2>Edit Availability Block #${id}</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 600px; margin-top: 15px;">
        <label>Modality Room *</label><select id="admin-slot-room" required></select><br><br>
        <div style="display: flex; gap: 15px;">
            <div style="flex:1"><label>Start Time *</label><input type="datetime-local" id="admin-slot-start" value="${d.slot_start_time.replace(' ', 'T').substring(0,16)}" required></div>
            <div style="flex:1"><label>End Time *</label><input type="datetime-local" id="admin-slot-end" value="${d.slot_end_time.replace(' ', 'T').substring(0,16)}" required></div>
        </div><br>
        <label>Status</label>
        <select id="admin-slot-status">
            <option value="AVAILABLE" ${d.slot_status==='AVAILABLE'?'selected':''}>AVAILABLE</option>
            <option value="BOOKED" ${d.slot_status==='BOOKED'?'selected':''}>BOOKED</option>
            <option value="BLOCKED_MAINTENANCE" ${d.slot_status==='BLOCKED_MAINTENANCE'?'selected':''}>BLOCKED_MAINTENANCE</option>
            <option value="RESERVED_STAT" ${d.slot_status==='RESERVED_STAT'?'selected':''}>RESERVED_STAT</option>
            <option value="COMPLETED" ${d.slot_status==='COMPLETED'?'selected':''}>COMPLETED</option>
            <option value="CANCELLED" ${d.slot_status==='CANCELLED'?'selected':''}>CANCELLED</option>
        </select><br><br>
        <button class="btn" onclick="submitEditAdminSlot(${id})">Update Slot</button>
        <div id="admin-slot-err" style="color:#e74c3c; font-weight: bold; margin-top: 10px;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
    const rooms = await execSQL(`SELECT modality_id, room_name FROM modalities_and_rooms ORDER BY room_name`);
    if (rooms && !rooms.error) document.getElementById('admin-slot-room').innerHTML = rooms.map(r => `<option value="${r.modality_id}" ${r.modality_id==d.modality_id?'selected':''}>${r.room_name}</option>`).join('');
}

async function submitEditAdminSlot(id) {
    const start = document.getElementById('admin-slot-start').value;
    const end = document.getElementById('admin-slot-end').value;
    if (!start || !end) return document.getElementById('admin-slot-err').innerText = "Times required.";
    const q = `UPDATE appointment_slots SET modality_id=${document.getElementById('admin-slot-room').value}, slot_start_time='${start.replace('T',' ')+':00'}', slot_end_time='${end.replace('T',' ')+':00'}', slot_status='${document.getElementById('admin-slot-status').value}' WHERE slot_id=${id}`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('admin-slot-err').innerText = data.error; else loadAdmin();
}

/* --- ICD-10 CLINICAL INDICATIONS --- */
function newIcd10UI() {
    let html = `<div class='header-flex'><h2>Add ICD-10 Code</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 600px; margin-top: 15px;">
        <label>ICD-10 Code *</label><input type="text" id="icd-code" required><br><br>
        <label>Short Description *</label><input type="text" id="icd-short" required><br><br>
        <label>Full Description</label><textarea id="icd-full" rows="3"></textarea><br>
        <button class="btn" onclick="submitIcd10()">Save</button><div id="icd-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitIcd10() {
    const c = document.getElementById('icd-code').value.trim().replace(/'/g, "''");
    const s = document.getElementById('icd-short').value.trim().replace(/'/g, "''");
    const f = document.getElementById('icd-full').value.trim().replace(/'/g, "''");
    if (!c || !s) return document.getElementById('icd-err').innerText = "Code and Short Description required.";
    const q = `INSERT INTO icd10_diagnoses (icd10_code, short_description, full_description) VALUES ('${c}', '${s}', '${f}')`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('icd-err').innerText = data.error; else loadAdmin();
}
async function editIcd10UI(code) {
    const res = await execSQL(`SELECT * FROM icd10_diagnoses WHERE icd10_code = '${code}'`);
    if (!res || !res.length) return; const d = res[0];
    let html = `<div class='header-flex'><h2>Edit ICD-10</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 600px; margin-top: 15px;">
        <label>ICD-10 Code (Read-Only)</label><input type="text" id="icd-code" value="${d.icd10_code}" readonly><br><br>
        <label>Short Description *</label><input type="text" id="icd-short" value="${d.short_description}" required><br><br>
        <label>Full Description</label><textarea id="icd-full" rows="3">${d.full_description || ''}</textarea><br>
        <button class="btn" onclick="submitEditIcd10('${code}')">Update</button><div id="icd-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitEditIcd10(code) {
    const s = document.getElementById('icd-short').value.trim().replace(/'/g, "''");
    const f = document.getElementById('icd-full').value.trim().replace(/'/g, "''");
    const q = `UPDATE icd10_diagnoses SET short_description='${s}', full_description='${f}' WHERE icd10_code='${code}'`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('icd-err').innerText = data.error; else loadAdmin();
}

/* --- REFERRING MD EDITOR --- */
function newReferringMdUI() {
    let html = `<div class='header-flex'><h2>Add Referring Physician</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Username *</label><input type="text" id="md-user"><br><br>
        <label>First Name *</label><input type="text" id="md-fn"> <label>Last Name *</label><input type="text" id="md-ln"><br><br>
        <label>Email *</label><input type="text" id="md-email"><br><br>
        <label>NPI/License</label><input type="text" id="md-npi"> <label>Credentials</label><input type="text" id="md-cred"> <label>Specialty</label><input type="text" id="md-spec"><br><br>
        <label>Phone</label><input type="text" id="md-phone"> <label>Fax</label><input type="text" id="md-fax"> <label>Comm Channel</label><select id="md-comm"><option value="HL7_EHR">HL7</option><option value="FAX">FAX</option></select><br>
        <button class="btn" onclick="submitReferringMd()">Save</button><div id="md-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitReferringMd() {
    const u = document.getElementById('md-user').value.trim().replace(/'/g, "''");
    const f = document.getElementById('md-fn').value.trim().replace(/'/g, "''");
    const l = document.getElementById('md-ln').value.trim().replace(/'/g, "''");
    const e = document.getElementById('md-email').value.trim().replace(/'/g, "''");
    if (!u || !f || !l || !e) return document.getElementById('md-err').innerText = "Missing required fields.";
    const q = `INSERT INTO staff_users (username, password_hash, role_id, first_name, last_name, email, npi_or_license, credentials, specialty, work_phone, fax_number, preferred_comm_channel, is_active) 
    VALUES ('${u}', 'default', 6, '${f}', '${l}', '${e}', '${document.getElementById('md-npi').value}', '${document.getElementById('md-cred').value}', '${document.getElementById('md-spec').value}', '${document.getElementById('md-phone').value}', '${document.getElementById('md-fax').value}', '${document.getElementById('md-comm').value}', 1)`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('md-err').innerText = data.error; else loadAdmin();
}
async function editReferringMdUI(id) {
    const res = await execSQL(`SELECT * FROM staff_users WHERE user_id = ${id}`);
    if (!res || !res.length) return; const d = res[0];
    let html = `<div class='header-flex'><h2>Edit Referring Physician #${id}</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Username *</label><input type="text" id="md-user" value="${d.username}"><br><br>
        <label>First Name *</label><input type="text" id="md-fn" value="${d.first_name}"> <label>Last Name *</label><input type="text" id="md-ln" value="${d.last_name}"><br><br>
        <label>Email *</label><input type="text" id="md-email" value="${d.email}"><br><br>
        <label>NPI/License</label><input type="text" id="md-npi" value="${d.npi_or_license||''}"> <label>Credentials</label><input type="text" id="md-cred" value="${d.credentials||''}"> <label>Specialty</label><input type="text" id="md-spec" value="${d.specialty||''}"><br><br>
        <label>Phone</label><input type="text" id="md-phone" value="${d.work_phone||''}"> <label>Fax</label><input type="text" id="md-fax" value="${d.fax_number||''}"> 
        <label>Comm Channel</label><select id="md-comm"><option value="HL7_EHR" ${d.preferred_comm_channel==='HL7_EHR'?'selected':''}>HL7</option><option value="FAX" ${d.preferred_comm_channel==='FAX'?'selected':''}>FAX</option></select><br>
        <button class="btn" onclick="submitEditReferringMd(${id})">Update</button><div id="md-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitEditReferringMd(id) {
    const u = document.getElementById('md-user').value.trim().replace(/'/g, "''");
    const f = document.getElementById('md-fn').value.trim().replace(/'/g, "''");
    const l = document.getElementById('md-ln').value.trim().replace(/'/g, "''");
    const e = document.getElementById('md-email').value.trim().replace(/'/g, "''");
    const q = `UPDATE staff_users SET username='${u}', first_name='${f}', last_name='${l}', email='${e}', npi_or_license='${document.getElementById('md-npi').value}', credentials='${document.getElementById('md-cred').value}', specialty='${document.getElementById('md-spec').value}', work_phone='${document.getElementById('md-phone').value}', fax_number='${document.getElementById('md-fax').value}', preferred_comm_channel='${document.getElementById('md-comm').value}' WHERE user_id=${id}`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('md-err').innerText = data.error; else loadAdmin();
}

/* --- EXAM CATALOG EDITOR --- */
function newExamUI() {
    let html = `<div class='header-flex'><h2>Add Procedure Exam</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Code *</label><input type="text" id="ex-code"> <label>Name *</label><input type="text" id="ex-name"><br><br>
        <label>Modality *</label><input type="text" id="ex-mod"> <label>Body Part *</label><input type="text" id="ex-body"> <label>Duration</label><input type="number" id="ex-dur" value="30"><br><br>
        <label>Prep Instructions</label><input type="text" id="ex-prep"><br>
        <button class="btn" onclick="submitExam()">Save</button><div id="ex-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitExam() {
    const c = document.getElementById('ex-code').value.trim().replace(/'/g, "''");
    const n = document.getElementById('ex-name').value.trim().replace(/'/g, "''");
    if (!c || !n) return document.getElementById('ex-err').innerText = "Code and Name required.";
    const q = `INSERT INTO exam_catalog (exam_code, exam_name, modality_type, body_part, default_duration_mins, patient_prep_instructions) VALUES ('${c}', '${n}', '${document.getElementById('ex-mod').value}', '${document.getElementById('ex-body').value}', ${document.getElementById('ex-dur').value||30}, '${document.getElementById('ex-prep').value}')`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('ex-err').innerText = data.error; else loadAdmin();
}
async function editExamUI(code) {
    const res = await execSQL(`SELECT * FROM exam_catalog WHERE exam_code = '${code}'`);
    if (!res || !res.length) return; const d = res[0];
    let html = `<div class='header-flex'><h2>Edit Procedure Exam</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Code (Read-Only)</label><input type="text" id="ex-code" value="${d.exam_code}" readonly> <label>Name *</label><input type="text" id="ex-name" value="${d.exam_name}"><br><br>
        <label>Modality *</label><input type="text" id="ex-mod" value="${d.modality_type}"> <label>Body Part *</label><input type="text" id="ex-body" value="${d.body_part}"> <label>Duration</label><input type="number" id="ex-dur" value="${d.default_duration_mins}"><br><br>
        <label>Prep Instructions</label><input type="text" id="ex-prep" value="${d.patient_prep_instructions||''}"><br>
        <button class="btn" onclick="submitEditExam('${code}')">Update</button><div id="ex-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitEditExam(code) {
    const n = document.getElementById('ex-name').value.trim().replace(/'/g, "''");
    const q = `UPDATE exam_catalog SET exam_name='${n}', modality_type='${document.getElementById('ex-mod').value}', body_part='${document.getElementById('ex-body').value}', default_duration_mins=${document.getElementById('ex-dur').value}, patient_prep_instructions='${document.getElementById('ex-prep').value}' WHERE exam_code='${code}'`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('ex-err').innerText = data.error; else loadAdmin();
}

/* --- FACILITIES EDITOR --- */
function newFacilityUI() {
    let html = `<div class='header-flex'><h2>Add Facility</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Code *</label><input type="text" id="fac-code"> <label>Name *</label><input type="text" id="fac-name"><br><br>
        <label>Address</label><input type="text" id="fac-addr"> <label>Phone</label><input type="text" id="fac-phone"><br>
        <button class="btn" onclick="submitFacility()">Save</button><div id="fac-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitFacility() {
    const c = document.getElementById('fac-code').value.trim().replace(/'/g, "''");
    if (!c) return document.getElementById('fac-err').innerText = "Code required.";
    const q = `INSERT INTO facilities (facility_code, facility_name, address, phone) VALUES ('${c}', '${document.getElementById('fac-name').value}', '${document.getElementById('fac-addr').value}', '${document.getElementById('fac-phone').value}')`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('fac-err').innerText = data.error; else loadAdmin();
}
async function editFacilityUI(id) {
    const res = await execSQL(`SELECT * FROM facilities WHERE facility_id = ${id}`);
    if (!res || !res.length) return; const d = res[0];
    let html = `<div class='header-flex'><h2>Edit Facility #${id}</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Code *</label><input type="text" id="fac-code" value="${d.facility_code}"> <label>Name *</label><input type="text" id="fac-name" value="${d.facility_name}"><br><br>
        <label>Address</label><input type="text" id="fac-addr" value="${d.address||''}"> <label>Phone</label><input type="text" id="fac-phone" value="${d.phone||''}"><br>
        <button class="btn" onclick="submitEditFacility(${id})">Update</button><div id="fac-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitEditFacility(id) {
    const c = document.getElementById('fac-code').value.trim().replace(/'/g, "''");
    const q = `UPDATE facilities SET facility_code='${c}', facility_name='${document.getElementById('fac-name').value}', address='${document.getElementById('fac-addr').value}', phone='${document.getElementById('fac-phone').value}' WHERE facility_id=${id}`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('fac-err').innerText = data.error; else loadAdmin();
}

/* --- MODALITIES & ROOMS EDITOR --- */
function newModalityUI() {
    let html = `<div class='header-flex'><h2>Add Modality Room</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Room Name *</label><input type="text" id="mod-room"> <label>Type *</label><input type="text" id="mod-type" value="CT"><br><br>
        <label>Facility ID</label><input type="number" id="mod-fac" value="1"> <label>AE Title *</label><input type="text" id="mod-aet"><br><br>
        <label>Status</label><select id="mod-status"><option value="ONLINE">ONLINE</option><option value="OFFLINE">OFFLINE</option></select><br>
        <button class="btn" onclick="submitModality()">Save</button><div id="mod-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitModality() {
    const r = document.getElementById('mod-room').value.trim().replace(/'/g, "''");
    const a = document.getElementById('mod-aet').value.trim().replace(/'/g, "''");
    if (!r || !a) return document.getElementById('mod-err').innerText = "Room and AET required.";
    const q = `INSERT INTO modalities_and_rooms (room_name, modality_type, facility_id, ae_title, operational_status) VALUES ('${r}', '${document.getElementById('mod-type').value}', ${document.getElementById('mod-fac').value}, '${a}', '${document.getElementById('mod-status').value}')`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('mod-err').innerText = data.error; else loadAdmin();
}
async function editModalityUI(id) {
    const res = await execSQL(`SELECT * FROM modalities_and_rooms WHERE modality_id = ${id}`);
    if (!res || !res.length) return; const d = res[0];
    let html = `<div class='header-flex'><h2>Edit Modality #${id}</h2><button class='btn btn-danger' onclick='loadAdmin()'>Cancel</button></div>
    <div style="background: #fff; padding: 20px; max-width: 800px; margin-top: 15px;">
        <label>Room Name *</label><input type="text" id="mod-room" value="${d.room_name}"> <label>Type *</label><input type="text" id="mod-type" value="${d.modality_type}"><br><br>
        <label>Facility ID</label><input type="number" id="mod-fac" value="${d.facility_id}"> <label>AE Title *</label><input type="text" id="mod-aet" value="${d.ae_title}"><br><br>
        <label>Status</label><select id="mod-status"><option value="ONLINE" ${d.operational_status==='ONLINE'?'selected':''}>ONLINE</option><option value="OFFLINE" ${d.operational_status==='OFFLINE'?'selected':''}>OFFLINE</option></select><br>
        <button class="btn" onclick="submitEditModality(${id})">Update</button><div id="mod-err" style="color:#e74c3c;"></div>
    </div>`;
    document.getElementById('main-content').innerHTML = html;
}
async function submitEditModality(id) {
    const r = document.getElementById('mod-room').value.trim().replace(/'/g, "''");
    const a = document.getElementById('mod-aet').value.trim().replace(/'/g, "''");
    const q = `UPDATE modalities_and_rooms SET room_name='${r}', modality_type='${document.getElementById('mod-type').value}', facility_id=${document.getElementById('mod-fac').value}, ae_title='${a}', operational_status='${document.getElementById('mod-status').value}' WHERE modality_id=${id}`;
    const data = await execSQL(q);
    if (data && data.error) document.getElementById('mod-err').innerText = data.error; else loadAdmin();
}function updateCmdPreview() {
    const sample = {
        '{to}': '416-555-4400 / dr@hospital.ca',
        '{fax}': '416-555-4400',
        '{patient}': 'Sarah Connor',
        '{mrn}': 'MRN-100501',
        '{exam}': 'CTA Chest',
        '{datetime}': '2026-11-20 09:30:00',
        '{room}': 'CT Suite 1',
        '{accession}': 'ACC-20261109-0001',
        '{doctor}': 'Dr. Arthur Pendelton',
        '{status}': 'FINAL',
        '{payload_file}': 'outbound_dispatches\\dispatch_1.txt',
        '{pdf_file}': 'outbound_dispatches\\report_1.pdf',
        '{message}': 'ACCESSION: ACC-20261109-0001 | IMPRESSION: ...'
    };
    [['rem','email'], ['rem','sms'], ['rem','tts'], ['rep','hl7'], ['rep','fax'], ['rep','fhir'], ['rep','portal']].forEach(([p, k]) => {
        const exe = document.getElementById(`cfg-${p}-${k}-exe`)?.value || '';
        let args = document.getElementById(`cfg-${p}-${k}-args`)?.value || '';
        Object.keys(sample).forEach(t => { args = args.split(t).join(sample[t]); });
        const prev = document.getElementById(`cfg-${p}-${k}-preview`);
        if (prev) prev.innerText = exe ? `"${exe}" ${args}` : '(Not configured)';
    });
}
