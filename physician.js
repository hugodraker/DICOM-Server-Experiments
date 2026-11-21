/* ============================================================================
 * RIS Physician & Radiologist Reading Queue + Reporting Workspace
 * ============================================================================ */

let g_physFilterTimer = null;
let g_physActiveUserId = null;
let g_physActiveRoleCode = null;
let g_physActiveOrderId = null;
let g_physLockTimer = null;
let g_physTemplates = [];
let g_physMacros = [];

let g_rsnaRecords = [];
let g_rsnaActiveRecord = null;

async function loadTemplateCdes(templateId, existingReportId = null) {
    const wrap = document.getElementById('rep-cde-container');
    if (!wrap) return;
    if (!templateId) { wrap.innerHTML = ''; return; }

    const [cdes, saved] = await Promise.all([
        execSQL(`SELECT * FROM radreport_cde_definitions WHERE template_id = ${templateId} ORDER BY cde_id`),
        existingReportId
            ? execSQL(`SELECT cde_id, selected_value FROM report_discrete_findings WHERE report_id = ${existingReportId}`)
            : Promise.resolve([])
    ]);

    if (!Array.isArray(cdes) || cdes.length === 0) { wrap.innerHTML = ''; return; }
    const savedMap = {};
    if (Array.isArray(saved)) saved.forEach(s => { savedMap[s.cde_id] = s.selected_value; });

    let html = `<div style="background:#f4f6f7; border:1px solid #d5dbdb; border-radius:4px; padding:10px; margin-bottom:10px;">
        <div style="font-size:12px; font-weight:bold; color:#2c3e50; margin-bottom:6px;">RadReport Discrete Findings (CDE)</div>
        <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(180px, 1fr)); gap:8px;">`;

    cdes.forEach(c => {
        const curVal = savedMap[c.cde_id] ?? '';
        let ctrl = '';
        if (c.data_type === 'SINGLE_SELECT' || c.data_type === 'BOOLEAN') {
            let opts = [];
            try { opts = JSON.parse(c.allowed_values_json || '[]'); } catch (e) {}
            ctrl = `<select class="rep-cde-input" data-cde-id="${escAttr(c.cde_id)}">
                <option value="">-- Select --</option>
                ${opts.map(o => `<option value="${escAttr(o)}" ${String(curVal) === String(o) ? 'selected' : ''}>${escAttr(o)}</option>`).join('')}
            </select>`;
        } else if (c.data_type === 'NUMERIC_MM') {
            ctrl = `<input type="number" step="0.1" class="rep-cde-input" data-cde-id="${escAttr(c.cde_id)}" value="${escAttr(curVal)}" placeholder="mm">`;
        } else {
            ctrl = `<input type="text" class="rep-cde-input" data-cde-id="${escAttr(c.cde_id)}" value="${escAttr(curVal)}">`;
        }
        html += `<div><label style="font-size:11px; font-weight:bold;">${escAttr(c.prompt_label)}</label>${ctrl}</div>`;
    });

    wrap.innerHTML = html + `</div></div>`;
}

async function previewPhysPaperReport(orderId, reportId = 0) {
    let overlay = document.getElementById('typst-preview-overlay');
    if (!overlay) {
        overlay = document.createElement('div');
        overlay.id = 'typst-preview-overlay';
        overlay.style.cssText = 'position:fixed; inset:24px; z-index:9999; background:#fff; border:2px solid #2c3e50; border-radius:6px; box-shadow:0 8px 30px rgba(0,0,0,0.45); display:flex; flex-direction:column; overflow:hidden;';
        document.body.appendChild(overlay);
    }

    overlay.innerHTML = `
        <div style="background:#2c3e50; color:#fff; padding:8px 12px; display:flex; justify-content:space-between; align-items:center; font-size:13px;">
            <b>Paper Report Preview (Typst JS Viewer)</b>
            <button type="button" class="btn btn-danger" style="margin:0; padding:3px 10px; font-size:12px;" onclick="document.getElementById('typst-preview-overlay')?.remove()">Close</button>
        </div>
        <div id="typst-preview-stage" style="flex:1; min-height:0;">Generating Typst preview...</div>`;

    try {
        if (!window.TypstJS) throw new Error('typst.js is not loaded.');

        const findEl = document.getElementById('rep-find');
        const imprEl = document.getElementById('rep-impr');
        const hasLiveText = !!(findEl?.value.trim() || imprEl?.value.trim());

        let typSrc = '';
        let fileName = `report_${reportId || orderId}.pdf`;

        if (reportId > 0 && !hasLiveText) {
            const res = await fetch('/api/report_typ?report_id=' + encodeURIComponent(reportId));
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            typSrc = await res.text();
        } else if (findEl && imprEl && orderId) {
            const ordRes = await execSQL(`
                SELECT o.accession_number, ec.exam_name, ec.modality_type,
                       p.last_name || ', ' || p.first_name AS pat_name, p.mrn, p.dob, p.biological_sex,
                       COALESCE(ref.first_name || ' ' || ref.last_name, 'N/A') AS ref_md,
                       COALESCE(f.facility_name, 'Diagnostic Imaging') AS fac_name,
                       COALESCE(f.address, '') AS fac_addr, COALESCE(f.phone, '') AS fac_phone
                FROM imaging_orders o
                JOIN patients p ON o.patient_id = p.patient_id
                JOIN exam_catalog ec ON o.exam_code = ec.exam_code
                LEFT JOIN staff_users ref ON o.referring_md_id = ref.user_id
                LEFT JOIN facilities f ON o.facility_id = f.facility_id
                WHERE o.order_id = ${orderId} LIMIT 1
            `);
            const o = Array.isArray(ordRes) && ordRes[0] ? ordRes[0] : {};
            const mod = o.modality_type || 'ALL';

            const tplRes = await execSQL(`
                SELECT typst_markup FROM typst_report_templates
                WHERE modality_type = ${sqlStr(mod)} OR modality_type = 'ALL' OR is_default = 1
                ORDER BY CASE WHEN modality_type = ${sqlStr(mod)} THEN 1 WHEN is_default = 1 THEN 2 ELSE 3 END
                LIMIT 1
            `);
            if (!Array.isArray(tplRes) || !tplRes[0]?.typst_markup) {
                throw new Error('No Typst report template found in typst_report_templates.');
            }

            const signerSel = document.getElementById('rep-signer');
            const signerTxt = signerSel?.options[signerSel.selectedIndex]?.text?.replace(/^Dr\.\s*/i, '') || 'Unsigned';
            const catSys = document.getElementById('rep-catsys')?.value || 'NONE';
            const catScore = (document.getElementById('rep-catscore')?.value || '').trim();
            const escT = s => String(s || '').replace(/\\/g, '\\\\').replace(/"/g, '\\"');
            const nowStr = new Date().toISOString().replace('T', ' ').slice(0, 19);
            fileName = `${o.accession_number || ('report_' + orderId)}.pdf`;

            const tokens = {
                '{facility_name}': escT(o.fac_name),
                '{facility_address}': escT(o.fac_addr),
                '{facility_phone}': escT(o.fac_phone),
                '{patient_name}': escT(o.pat_name),
                '{accession}': escT(o.accession_number),
                '{mrn}': escT(o.mrn),
                '{dob}': escT(o.dob),
                '{sex}': escT(o.biological_sex),
                '{exam_name}': escT(o.exam_name),
                '{modality}': escT(o.modality_type),
                '{referring_md}': escT(o.ref_md),
                '{radiologist}': escT(signerTxt),
                '{radiologist_license}': 'DRAFT',
                '{report_status}': 'DRAFT PREVIEW',
                '{version}': '1',
                '{signed_at}': nowStr,
                '{clinical_history}': document.getElementById('rep-hist')?.value.trim() || 'None provided.',
                '{comparison}': document.getElementById('rep-comp')?.value.trim() || 'None available.',
                '{technique}': document.getElementById('rep-tech')?.value.trim() || '(Pending technique)',
                '{findings}': findEl.value.trim() || '(Pending findings)',
                '{impression}': imprEl.value.trim() || '(Pending impression)',
                '{standardized_assessment}': catSys !== 'NONE' ? `*Standardized Assessment:* ${catSys} ${catScore}\n` : ''
            };

            typSrc = tplRes[0].typst_markup;
            Object.entries(tokens).forEach(([k, v]) => { typSrc = typSrc.split(k).join(v); });
        } else if (reportId > 0) {
            const res = await fetch('/api/report_typ?report_id=' + encodeURIComponent(reportId));
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            typSrc = await res.text();
        } else {
            throw new Error('No report content available to preview.');
        }

        TypstJS.mountViewer('typst-preview-stage', typSrc, { filename: fileName });
    } catch (e) {
        const stage = document.getElementById('typst-preview-stage');
        if (stage) stage.innerHTML = `<div style="padding:16px; color:#c0392b; font-weight:bold;">Failed to render paper report: ${e.message}</div>`;
    }
}
async function saveTemplateCdes(reportId) {
    if (!reportId) return;
    const inputs = document.querySelectorAll('.rep-cde-input');
    for (const el of inputs) {
        const cdeId = el.dataset.cdeId;
        const val = (el.value || '').trim();
        if (!cdeId || !val) continue;
        const numScore = isNaN(Number(val)) ? 0 : Math.round(Number(val));
        await execSQL(`
            INSERT OR REPLACE INTO report_discrete_findings (report_id, cde_id, selected_value, numeric_score)
            VALUES (${reportId}, ${sqlStr(cdeId)}, ${sqlStr(val)}, ${numScore})
        `);
    }
}

function unescapeRsnaNewlines(str) {
    return (str || '').replace(/\\n/g, '\n');
}

async function initRsnaDropdownEngine(defaultModality = '') {
    g_rsnaActiveRecord = null;
    const modSel = document.getElementById('rsna-mod');
    const anatSel = document.getElementById('rsna-anat');
    const pathSel = document.getElementById('rsna-path');
    if (!modSel || !anatSel || !pathSel) return;

    anatSel.innerHTML = `<option value="">-- Select Anatomy --</option>`;
    pathSel.innerHTML = `<option value="">-- Select Pathology --</option>`;
    hideAllRsnaVars();

    try {
        const rows = await execSQL(`
            SELECT DISTINCT modality
            FROM rsna_reporting_templates
            WHERE TRIM(COALESCE(modality, '')) != ''
            ORDER BY modality
        `);
        if (rows && rows.error) {
            modSel.innerHTML = `<option value="">SQL Error</option>`;
            setPhysBanner('read-status-bar', `RSNA templates error: ${rows.error}`, true);
            return;
        }

        const uniqueMods = Array.isArray(rows) ? rows.map(r => r.modality) : [];
        modSel.innerHTML = `<option value="">-- Select Modality (${uniqueMods.length}) --</option>` +
            uniqueMods.map(m => `<option value="${escAttr(m)}">${escAttr(m)}</option>`).join('');

        if (defaultModality) {
            const match = uniqueMods.find(m => m.toUpperCase() === defaultModality.toUpperCase());
            if (match) {
                modSel.value = match;
                await onRsnaModalityChange();
            }
        }
    } catch (e) {
        modSel.innerHTML = `<option value="">Load Error</option>`;
        setPhysBanner('read-status-bar', `Failed to load RSNA modalities: ${e.message}`, true);
    }
}

function hideAllRsnaVars() {
    for (let i = 0; i < 4; i++) {
        const wrap = document.getElementById(`rsna-var-wrap-${i}`);
        const sel = document.getElementById(`rsna-var-${i}`);
        if (wrap) wrap.style.display = 'none';
        if (sel) sel.innerHTML = '';
    }
}

async function onRsnaModalityChange() {
    const mod = document.getElementById('rsna-mod')?.value || '';
    const anatSel = document.getElementById('rsna-anat');
    const pathSel = document.getElementById('rsna-path');
    g_rsnaActiveRecord = null;
    hideAllRsnaVars();
    pathSel.innerHTML = `<option value="">-- Select Pathology --</option>`;

    if (!mod) {
        anatSel.innerHTML = `<option value="">-- Select Anatomy --</option>`;
        return;
    }

    const rows = await execSQL(`
        SELECT DISTINCT anatomy
        FROM rsna_reporting_templates
        WHERE modality = ${sqlStr(mod)}
          AND TRIM(COALESCE(anatomy, '')) != ''
        ORDER BY anatomy
    `);
    const uniqueAnats = Array.isArray(rows) ? rows.map(r => r.anatomy) : [];
    anatSel.innerHTML = `<option value="">-- Select Anatomy (${uniqueAnats.length}) --</option>` +
        uniqueAnats.map(a => `<option value="${escAttr(a)}">${escAttr(a)}</option>`).join('');
}

async function onRsnaAnatomyChange() {
    const mod = document.getElementById('rsna-mod')?.value || '';
    const anat = document.getElementById('rsna-anat')?.value || '';
    const pathSel = document.getElementById('rsna-path');
    g_rsnaActiveRecord = null;
    hideAllRsnaVars();

    if (!mod || !anat) {
        pathSel.innerHTML = `<option value="">-- Select Pathology --</option>`;
        return;
    }

    const rows = await execSQL(`
        SELECT DISTINCT pathology
        FROM rsna_reporting_templates
        WHERE modality = ${sqlStr(mod)}
          AND anatomy = ${sqlStr(anat)}
          AND TRIM(COALESCE(pathology, '')) != ''
        ORDER BY pathology
    `);
    const uniquePaths = Array.isArray(rows) ? rows.map(r => r.pathology) : [];
    pathSel.innerHTML = `<option value="">-- Select Pathology (${uniquePaths.length}) --</option>` +
        uniquePaths.map(p => `<option value="${escAttr(p)}">${escAttr(p)}</option>`).join('');
}

async function onRsnaPathologyChange() {
    const mod = document.getElementById('rsna-mod')?.value || '';
    const anat = document.getElementById('rsna-anat')?.value || '';
    const path = document.getElementById('rsna-path')?.value || '';

    g_rsnaActiveRecord = null;
    hideAllRsnaVars();
    if (!mod || !anat || !path) return;

    try {
        const rows = await execSQL(`
            SELECT *
            FROM rsna_reporting_templates
            WHERE modality = ${sqlStr(mod)}
              AND anatomy = ${sqlStr(anat)}
              AND pathology = ${sqlStr(path)}
            LIMIT 1
        `);
        if (!Array.isArray(rows) || !rows.length) return;
        g_rsnaActiveRecord = rows[0];
    } catch (e) {
        setPhysBanner('read-status-bar', `Template JSON parse error: ${e.message}`, true);
        return;
    }

    const vars = [
        { lbl: g_rsnaActiveRecord.v1_label, vals: g_rsnaActiveRecord.v1_values },
        { lbl: g_rsnaActiveRecord.v2_label, vals: g_rsnaActiveRecord.v2_values },
        { lbl: g_rsnaActiveRecord.v3_label, vals: g_rsnaActiveRecord.v3_values },
        { lbl: g_rsnaActiveRecord.v4_label, vals: g_rsnaActiveRecord.v4_values }
    ];

    vars.forEach((v, i) => {
        if (v.lbl && v.vals) {
            const wrap = document.getElementById(`rsna-var-wrap-${i}`);
            const lblEl = document.getElementById(`rsna-var-lbl-${i}`);
            const selEl = document.getElementById(`rsna-var-${i}`);
            const options = v.vals.split(';').map(s => s.trim()).filter(Boolean);
            if (options.length > 0 && wrap && lblEl && selEl) {
                lblEl.innerText = v.lbl;
                selEl.innerHTML = options.map(opt => `<option value="${escAttr(opt)}">${escAttr(opt)}</option>`).join('');
                wrap.style.display = 'block';
            }
        }
    });

    generateRsnaReport();
}

function generateRsnaReport() {
    if (!g_rsnaActiveRecord) return;

    let baseText = unescapeRsnaNewlines(g_rsnaActiveRecord.base_template);
    let findText = unescapeRsnaNewlines(g_rsnaActiveRecord.findings_template);
    let impText = unescapeRsnaNewlines(g_rsnaActiveRecord.impression_template);

    const keys = [
        g_rsnaActiveRecord.v1_key,
        g_rsnaActiveRecord.v2_key,
        g_rsnaActiveRecord.v3_key,
        g_rsnaActiveRecord.v4_key
    ];

    keys.forEach((key, i) => {
        const selEl = document.getElementById(`rsna-var-${i}`);
        if (key && selEl && selEl.value) {
            findText = findText.split(key).join(selEl.value);
            impText = impText.split(key).join(selEl.value);
        }
    });

    const assembled = baseText
        .split('[FINDINGS_INSERT]').join(findText)
        .split('[IMPRESSION_INSERT]').join(impText);

    const findEl = document.getElementById('rep-find');
    const imprEl = document.getElementById('rep-impr');
    const techEl = document.getElementById('rep-tech');
    const fullPreviewEl = document.getElementById('rsna-full-preview');

    if (findEl) findEl.value = findText;
    if (imprEl) imprEl.value = impText;
    if (techEl && !techEl.value.trim()) {
        techEl.value = `${g_rsnaActiveRecord.modality} ${g_rsnaActiveRecord.anatomy} protocol.`;
    }
    if (fullPreviewEl) {
        fullPreviewEl.value = assembled;
        fullPreviewEl.dataset.assembled = assembled;
    }
}

function applyRsnaBaseToFindings() {
    const fullPreviewEl = document.getElementById('rsna-full-preview');
    const findEl = document.getElementById('rep-find');
    if (fullPreviewEl && findEl && fullPreviewEl.value.trim()) {
        findEl.value = fullPreviewEl.value;
    }
}

const PHYS_PRIO_COLORS = {
    CODE_STROKE_TRAUMA: '#8e44ad',
    STAT: '#e74c3c',
    URGENT: '#e67e22',
    ROUTINE: '#3498db'
};

function stopPhysLockHeartbeat() {
    if (g_physLockTimer) {
        clearInterval(g_physLockTimer);
        g_physLockTimer = null;
    }
}

async function releasePhysLock(orderId) {
    stopPhysLockHeartbeat();
    if (!orderId || !g_physActiveUserId) return;
    await execSQL(`DELETE FROM worklist_locks WHERE order_id = ${orderId} AND locked_by_user_id = ${g_physActiveUserId}`);
}

async function acquirePhysLock(orderId) {
    stopPhysLockHeartbeat();
    if (!orderId || !g_physActiveUserId) return { ok: true };

    const existing = await execSQL(`
        SELECT l.locked_by_user_id, l.heartbeat_expires_at, u.first_name || ' ' || u.last_name AS locker_name
        FROM worklist_locks l
        JOIN staff_users u ON l.locked_by_user_id = u.user_id
        WHERE l.order_id = ${orderId} AND l.heartbeat_expires_at > datetime('now')
    `);

    if (Array.isArray(existing) && existing.length > 0 && String(existing[0].locked_by_user_id) !== String(g_physActiveUserId)) {
        return { ok: false, lockedBy: existing[0].locker_name };
    }

    await execSQL(`
        INSERT OR REPLACE INTO worklist_locks (order_id, locked_by_user_id, workstation_ip, locked_at, heartbeat_expires_at)
        VALUES (${orderId}, ${g_physActiveUserId}, '${location.hostname || '127.0.0.1'}', datetime('now'), datetime('now', '+15 minutes'))
    `);

    g_physLockTimer = setInterval(async () => {
        if (!document.getElementById('phys-report-form') || String(g_physActiveOrderId) !== String(orderId)) {
            stopPhysLockHeartbeat();
            return;
        }
        await execSQL(`
            UPDATE worklist_locks
            SET heartbeat_expires_at = datetime('now', '+15 minutes')
            WHERE order_id = ${orderId} AND locked_by_user_id = ${g_physActiveUserId}
        `);
    }, 60000);

    return { ok: true };
}

async function logPhiAccess(patientId, orderId, action, reason = '') {
    if (!g_physActiveUserId || !patientId) return;
    const safeReason = String(reason || '').replace(/'/g, "''");
    const ordVal = orderId ? orderId : 'NULL';
    await execSQL(`
        INSERT INTO hipaa_phi_access_logs (user_id, patient_id, order_id, access_action, workstation_ip, access_reason)
        VALUES (${g_physActiveUserId}, ${patientId}, ${ordVal}, '${action}', '${location.hostname || '127.0.0.1'}', '${safeReason}')
    `);
}

function setPhysBanner(elId, msg, isError = false) {
    const el = document.getElementById(elId);
    if (!el) return;
    if (!msg) { el.style.display = 'none'; return; }
    el.style.display = 'block';
    el.style.background = isError ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isError ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isError ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

/* ============================================================================
 * 1. PHYSICIAN & RADIOLOGIST READING QUEUE
 * ============================================================================ */
async function loadPhysicianQueue() {
    if (g_physActiveOrderId) {
        await releasePhysLock(g_physActiveOrderId);
        g_physActiveOrderId = null;
    }

    const docs = await execSQL(`
        SELECT u.user_id, u.first_name, u.last_name, u.credentials, u.specialty, r.role_code,
               r.can_draft_reports, r.can_sign_final_reports
        FROM staff_users u
        JOIN roles r ON u.role_id = r.role_id
        WHERE u.is_active = 1 AND r.role_code IN ('RADIOLOGIST', 'RESIDENT_FELLOW', 'REFERRING_MD')
        ORDER BY CASE r.role_code WHEN 'RADIOLOGIST' THEN 1 WHEN 'RESIDENT_FELLOW' THEN 2 ELSE 3 END, u.last_name
    `);

    const docList = Array.isArray(docs) ? docs : [];
    if (!g_physActiveUserId && docList.length > 0) {
        g_physActiveUserId = docList[0].user_id;
        g_physActiveRoleCode = docList[0].role_code;
    } else if (g_physActiveUserId) {
        const cur = docList.find(d => String(d.user_id) === String(g_physActiveUserId));
        if (cur) g_physActiveRoleCode = cur.role_code;
    }

    const docOpts = docList.map(d =>
        `<option value="${d.user_id}" data-role="${d.role_code}" ${String(d.user_id) === String(g_physActiveUserId) ? 'selected' : ''}>Dr. ${d.first_name} ${d.last_name}${d.credentials ? ', ' + d.credentials : ''} [${d.role_code}]</option>`
    ).join('');

    const radOpts = docList.filter(d => d.role_code === 'RADIOLOGIST' || d.role_code === 'RESIDENT_FELLOW').map(d =>
        `<option value="${d.user_id}">Dr. ${d.last_name}, ${d.first_name}</option>`
    ).join('');

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Physician &amp; Radiologist Reading Queue</h2>
        <div style="display:flex; gap:8px; align-items:center; flex-wrap:wrap;">
            <label style="font-size:12px; font-weight:bold;">Active Clinician:</label>
            <select id="phys-active-user" style="width:auto; min-width:240px;" onchange="onPhysActiveUserChange(this)">
                ${docOpts || '<option value="">No physicians configured</option>'}
            </select>
            <button class="btn" style="background:#c0392b; margin:0;" onclick="loadCriticalAlertsView()">Critical Alerts</button>
            <button class="btn" style="background:#3498db; margin:0;" onclick="refreshPhysicianQueue()">Refresh</button>
        </div>
    </div>

    <div id="phys-status-bar" style="display:none; margin-top:10px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div id="phys-critical-strip" style="margin-top:10px;"></div>
    <div id="phys-kpi-bar" style="display:grid; grid-template-columns:repeat(auto-fit, minmax(145px, 1fr)); gap:10px; margin:12px 0;"></div>

    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px;">
        <div style="flex:2; min-width:190px;">
            <label style="font-size:12px; font-weight:bold;">Search Queue</label>
            <input type="text" id="phys-flt-search" placeholder="Patient, MRN, accession, indication..." oninput="schedulePhysFilter()">
        </div>
        <div style="min-width:155px;">
            <label style="font-size:12px; font-weight:bold;">Queue Bucket</label>
            <select id="phys-flt-bucket" onchange="refreshPhysicianQueue()">
                <option value="READING" selected>Reading Queue (Unread/Prelim)</option>
                <option value="READY_FOR_READ">Ready for Read Only</option>
                <option value="ASSIGNED_READING">In Progress Reading</option>
                <option value="PRELIMINARY_REPORTED">Preliminary (Needs Final)</option>
                <option value="FINAL_REPORTED">Finalized / Addendum</option>
                <option value="ALL_ACTIVE">All Active Orders</option>
            </select>
        </div>
        <div style="min-width:120px;">
            <label style="font-size:12px; font-weight:bold;">Modality</label>
            <select id="phys-flt-mod" onchange="refreshPhysicianQueue()">
                <option value="ALL">All Modalities</option>
                <option value="CT">CT</option>
                <option value="MRI">MRI</option>
                <option value="XRAY">XRAY</option>
                <option value="US">US</option>
                <option value="MAMMO">MAMMO</option>
                <option value="FLUORO">FLUORO</option>
                <option value="NM">NM</option>
                <option value="PET">PET</option>
                <option value="DEXA">DEXA</option>
            </select>
        </div>
        <div style="min-width:130px;">
            <label style="font-size:12px; font-weight:bold;">Priority</label>
            <select id="phys-flt-prio" onchange="refreshPhysicianQueue()">
                <option value="ALL">All Priorities</option>
                <option value="CODE_STROKE_TRAUMA">CODE_STROKE_TRAUMA</option>
                <option value="STAT">STAT</option>
                <option value="URGENT">URGENT</option>
                <option value="ROUTINE">ROUTINE</option>
            </select>
        </div>
        <div style="min-width:150px;">
            <label style="font-size:12px; font-weight:bold;">Assigned To</label>
            <select id="phys-flt-rad" onchange="refreshPhysicianQueue()">
                <option value="ALL">Anyone</option>
                <option value="MINE">Assigned to Me</option>
                <option value="UNASSIGNED">Unassigned</option>
                ${radOpts}
            </select>
        </div>
        <div>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="clearPhysFilters()">Clear</button>
        </div>
    </div>

    <div id="phys-table-wrap">Loading...</div>`;

    refreshPhysicianQueue();
}

function onPhysActiveUserChange(sel) {
    g_physActiveUserId = sel.value ? parseInt(sel.value, 10) : null;
    g_physActiveRoleCode = sel.options[sel.selectedIndex]?.dataset.role || null;
    refreshPhysicianQueue();
}

function schedulePhysFilter() {
    clearTimeout(g_physFilterTimer);
    g_physFilterTimer = setTimeout(refreshPhysicianQueue, 150);
}

function clearPhysFilters() {
    const setVal = (id, v) => { const el = document.getElementById(id); if (el) el.value = v; };
    setVal('phys-flt-search', '');
    setVal('phys-flt-bucket', 'READING');
    setVal('phys-flt-mod', 'ALL');
    setVal('phys-flt-prio', 'ALL');
    setVal('phys-flt-rad', 'ALL');
    refreshPhysicianQueue();
}

function setPhysQuickFilter(bucket, prio = 'ALL') {
    const b = document.getElementById('phys-flt-bucket');
    const p = document.getElementById('phys-flt-prio');
    if (b) b.value = bucket;
    if (p) p.value = prio;
    refreshPhysicianQueue();
}

async function refreshPhysicianQueue() {
    const wrap = document.getElementById('phys-table-wrap');
    if (!wrap) return;

    const search = (document.getElementById('phys-flt-search')?.value || '').trim().replace(/'/g, "''");
    const bucket = document.getElementById('phys-flt-bucket')?.value || 'READING';
    const mod = document.getElementById('phys-flt-mod')?.value || 'ALL';
    const prio = document.getElementById('phys-flt-prio')?.value || 'ALL';
    const rad = document.getElementById('phys-flt-rad')?.value || 'ALL';

    const where = [];
    if (bucket === 'READING') {
        where.push(`o.workflow_status IN ('IMAGES_CAPTURED_QA', 'READY_FOR_READ', 'ASSIGNED_READING', 'PRELIMINARY_REPORTED')`);
    } else if (bucket === 'FINAL_REPORTED') {
        where.push(`o.workflow_status IN ('FINAL_REPORTED', 'RESULTS_DELIVERED', 'CLOSED')`);
    } else if (bucket === 'ALL_ACTIVE') {
        where.push(`o.workflow_status NOT IN ('CANCELLED', 'NO_SHOW')`);
    } else {
        where.push(`o.workflow_status = '${bucket}'`);
    }

    if (mod !== 'ALL') where.push(`ec.modality_type = '${mod}'`);
    if (prio !== 'ALL') where.push(`o.priority = '${prio}'`);
    if (rad === 'MINE' && g_physActiveUserId) {
        where.push(`(o.assigned_radiologist_id = ${g_physActiveUserId} OR o.assigned_resident_id = ${g_physActiveUserId})`);
    } else if (rad === 'UNASSIGNED') {
        where.push(`o.assigned_radiologist_id IS NULL`);
    } else if (rad !== 'ALL' && rad !== 'MINE') {
        where.push(`(o.assigned_radiologist_id = ${rad} OR o.assigned_resident_id = ${rad})`);
    }

    if (search) {
        where.push(`(
            p.first_name || ' ' || p.last_name LIKE '%${search}%' OR
            p.last_name || ', ' || p.first_name LIKE '%${search}%' OR
            p.mrn LIKE '%${search}%' OR
            o.accession_number LIKE '%${search}%' OR
            o.clinical_indication LIKE '%${search}%' OR
            ec.exam_name LIKE '%${search}%'
        )`);
    }

    const whereClause = where.length ? `WHERE ${where.join(' AND ')}` : '';

    const [rows, kpis, critAlerts] = await Promise.all([
        execSQL(`
            SELECT o.order_id,
                   o.accession_number,
                   o.priority,
                   o.patient_class,
                   o.workflow_status,
                   o.clinical_indication,
                   p.patient_id,
                   p.mrn,
                   p.last_name || ', ' || p.first_name AS patient_name,
                   p.dob,
                   p.biological_sex,
                   ec.modality_type,
                   ec.exam_name,
                   COALESCE(ds.total_instance_count, 0) AS slice_count,
                   COALESCE(ds.pacs_zero_footprint_viewer_url, '') AS viewer_url,
                   COALESCE(rad.last_name, '') AS assigned_rad,
                   COALESCE(res.last_name, '') AS assigned_res,
                   CAST((julianday('now') - julianday(COALESCE(o.qa_completed_at, o.scan_completed_at, o.ordered_at))) * 1440 AS INTEGER) AS mins_waiting,
                   (SELECT r.report_status FROM radiology_reports r WHERE r.order_id = o.order_id ORDER BY r.report_id DESC LIMIT 1) AS latest_report_status,
                   (SELECT u.last_name FROM worklist_locks wl JOIN staff_users u ON wl.locked_by_user_id = u.user_id WHERE wl.order_id = o.order_id AND wl.heartbeat_expires_at > datetime('now')) AS locked_by,
                   (SELECT COUNT(*) FROM critical_findings_escalations cfe WHERE cfe.order_id = o.order_id AND cfe.escalation_status != 'ACKNOWLEDGED_CLOSED') AS open_criticals
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            LEFT JOIN dicom_studies ds ON o.order_id = ds.order_id
            LEFT JOIN staff_users rad ON o.assigned_radiologist_id = rad.user_id
            LEFT JOIN staff_users res ON o.assigned_resident_id = res.user_id
            ${whereClause}
            ORDER BY
                CASE o.priority
                    WHEN 'CODE_STROKE_TRAUMA' THEN 1
                    WHEN 'STAT' THEN 2
                    WHEN 'URGENT' THEN 3
                    ELSE 4
                END,
                COALESCE(o.qa_completed_at, o.scan_completed_at, o.ordered_at) ASC
            LIMIT 150
        `),
        execSQL(`
            SELECT
                SUM(CASE WHEN workflow_status IN ('IMAGES_CAPTURED_QA', 'READY_FOR_READ') THEN 1 ELSE 0 END) AS unread_cnt,
                SUM(CASE WHEN workflow_status IN ('IMAGES_CAPTURED_QA', 'READY_FOR_READ', 'ASSIGNED_READING') AND priority IN ('STAT', 'CODE_STROKE_TRAUMA') THEN 1 ELSE 0 END) AS stat_cnt,
                SUM(CASE WHEN workflow_status = 'PRELIMINARY_REPORTED' THEN 1 ELSE 0 END) AS prelim_cnt,
                SUM(CASE WHEN workflow_status IN ('FINAL_REPORTED', 'RESULTS_DELIVERED') THEN 1 ELSE 0 END) AS final_cnt
            FROM imaging_orders
        `),
        execSQL(`SELECT COUNT(*) AS cnt FROM critical_findings_escalations WHERE escalation_status != 'ACKNOWLEDGED_CLOSED'`)
    ]);

    const critCnt = (Array.isArray(critAlerts) && critAlerts[0]?.cnt) ? critAlerts[0].cnt : 0;
    const critStrip = document.getElementById('phys-critical-strip');
    if (critStrip) {
        critStrip.innerHTML = critCnt > 0
            ? `<div style="background:#fdedec; border:1px solid #e74c3c; border-left:5px solid #c0392b; border-radius:4px; padding:8px 12px; display:flex; justify-content:space-between; align-items:center; font-size:13px;">
                <span style="color:#922b21; font-weight:bold;">CRITICAL SLA ALERT: ${critCnt} unacknowledged critical finding(s) awaiting physician verbatim read-back.</span>
                <button class="btn" style="background:#c0392b; margin:0; padding:4px 10px; font-size:12px;" onclick="loadCriticalAlertsView()">Review &amp; Acknowledge</button>
               </div>`
            : '';
    }

    const kpiBar = document.getElementById('phys-kpi-bar');
    if (kpiBar && Array.isArray(kpis) && kpis.length) {
        const k = kpis[0];
        const card = (lbl, val, col, onclickJs) => `
            <div onclick="${onclickJs}" style="cursor:pointer; background:#fff; border-left:4px solid ${col}; border-radius:4px; padding:10px 12px; box-shadow:0 1px 3px rgba(0,0,0,0.08);">
                <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">${lbl}</div>
                <div style="font-size:20px; font-weight:bold; color:${col}; margin-top:2px;">${val || 0}</div>
            </div>`;
        kpiBar.innerHTML =
            card('Unread / Ready', k.unread_cnt, '#2980b9', `setPhysQuickFilter('READY_FOR_READ','ALL')`) +
            card('STAT / Stroke Unread', k.stat_cnt, '#e74c3c', `setPhysQuickFilter('READING','STAT')`) +
            card('Preliminary (Sign-off)', k.prelim_cnt, '#f39c12', `setPhysQuickFilter('PRELIMINARY_REPORTED','ALL')`) +
            card('Finalized Reports', k.final_cnt, '#27ae60', `setPhysQuickFilter('FINAL_REPORTED','ALL')`);
    }

    if (rows && rows.error) {
        wrap.innerHTML = `<p style="color:#e74c3c; font-weight:bold;">Database Error: ${rows.error}</p>`;
        return;
    }
    if (!Array.isArray(rows) || rows.length === 0) {
        wrap.innerHTML = '<p style="background:#fff; padding:16px; border-radius:4px;">No studies match the current reading queue filters.</p>';
        return;
    }

    const cellStyle = 'padding:6px 5px; font-size:12px; word-break:break-word; overflow-wrap:anywhere; border-bottom:1px solid #ecf0f1;';
    const thStyle = `${cellStyle} background:#2c3e50; color:#fff; text-align:left;`;
    const btnStyle = 'padding:3px 7px; font-size:11px; margin:1px 2px 1px 0;';

    let html = `
    <table style="width:100%; table-layout:fixed; border-collapse:collapse; background:#fff;">
        <thead><tr>
            <th style="width:9%; ${thStyle}">PRIORITY</th>
            <th style="width:12%; ${thStyle}">ACCESSION</th>
            <th style="width:14%; ${thStyle}">PATIENT</th>
            <th style="width:16%; ${thStyle}">EXAM / MODALITY</th>
            <th style="width:16%; ${thStyle}">INDICATION</th>
            <th style="width:7%; ${thStyle}">IMAGES</th>
            <th style="width:11%; ${thStyle}">STATUS / RAD</th>
            <th style="width:15%; ${thStyle}">ACTIONS</th>
        </tr></thead>
        <tbody>`;

    rows.forEach(r => {
        const pCol = PHYS_PRIO_COLORS[r.priority] || '#3498db';
        const waitTxt = r.mins_waiting !== null ? `${r.mins_waiting}m` : '-';
        const radTxt = r.assigned_rad ? `Dr. ${r.assigned_rad}` : (r.assigned_res ? `Res. ${r.assigned_res}` : 'Unassigned');
        html += `
        <tr style="${r.open_criticals > 0 ? 'background:#fff5f5;' : ''}">
            <td style="${cellStyle}">
                <span style="background:${pCol}; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px; font-weight:bold; display:inline-block;">${r.priority}</span>
                <div style="font-size:10px; color:#7f8c8d; margin-top:2px;">${r.patient_class} &bull; ${waitTxt}</div>
            </td>
            <td style="${cellStyle}">
                <b>${escAttr(r.accession_number)}</b>
                ${r.locked_by ? `<div style="font-size:10px; color:#d35400; font-weight:bold;">Locked: Dr. ${escAttr(r.locked_by)}</div>` : ''}
                ${r.open_criticals > 0 ? `<div style="font-size:10px; color:#c0392b; font-weight:bold;">CRITICAL OPEN</div>` : ''}
            </td>
            <td style="${cellStyle}">
                <b>${escAttr(r.patient_name)}</b>
                <div style="font-size:10px; color:#7f8c8d;">${escAttr(r.mrn)} &bull; ${r.biological_sex} &bull; ${r.dob}</div>
            </td>
            <td style="${cellStyle}">
                <span style="background:#34495e; color:#fff; padding:1px 4px; border-radius:3px; font-size:10px; font-weight:bold;">${r.modality_type}</span>
                <div style="margin-top:2px;">${escAttr(r.exam_name)}</div>
            </td>
            <td style="${cellStyle}">${escAttr(r.clinical_indication)}</td>
            <td style="${cellStyle}"><b>${r.slice_count}</b> img${r.slice_count === 1 ? '' : 's'}</td>
            <td style="${cellStyle}">
                <div style="font-weight:bold; font-size:11px;">${r.workflow_status}</div>
                <div style="font-size:10px; color:#555;">${escAttr(radTxt)}${r.latest_report_status ? ` (${r.latest_report_status})` : ''}</div>
            </td>
            <td style="${cellStyle}">
                <button class="btn" style="background:#2980b9; ${btnStyle}" onclick="openReadingWorkspace(${r.order_id})">Read / Report</button>
            </td>
        </tr>`;
    });

    wrap.innerHTML = html + '</tbody></table>';
}

/* ============================================================================
 * 2. SPLIT-PANE READING & REPORTING WORKSPACE
 * ============================================================================ */
async function openReadingWorkspace(orderId) {
    const lockRes = await acquirePhysLock(orderId);
    g_physActiveOrderId = orderId;

    const [ordRes, studyRes, seriesRes, repRes, tplRes, macroRes, radsRes, examsRes] = await Promise.all([
        execSQL(`
            SELECT o.*, p.mrn, p.first_name, p.last_name, p.dob, p.biological_sex, p.weight_kg,
                   p.allergies, p.contrast_allergy_flag, p.contrast_allergy_details, p.egfr_value, p.creatinine_value,
                   ec.exam_name, ec.modality_type, ec.body_part,
                   COALESCE(ref.first_name || ' ' || ref.last_name, 'Unknown') AS ref_md_name,
                   COALESCE(ref.work_phone, '') AS ref_md_phone
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            LEFT JOIN staff_users ref ON o.referring_md_id = ref.user_id
            WHERE o.order_id = ${orderId}
        `),
        execSQL(`SELECT * FROM dicom_studies WHERE order_id = ${orderId} LIMIT 1`),
        execSQL(`
            SELECT ds.* FROM dicom_series ds
            JOIN dicom_studies st ON ds.study_id = st.study_id
            WHERE st.order_id = ${orderId}
            ORDER BY ds.series_number ASC
        `),
        execSQL(`
            SELECT r.*, u.first_name || ' ' || u.last_name AS signer_name
            FROM radiology_reports r
            LEFT JOIN staff_users u ON r.signing_radiologist_id = u.user_id
            WHERE r.order_id = ${orderId}
            ORDER BY r.version_number DESC, r.report_id DESC
        `),
        execSQL(`SELECT * FROM report_templates ORDER BY template_name`),
        execSQL(`SELECT * FROM radiologist_voice_macros ORDER BY trigger_phrase`),
        execSQL(`
            SELECT u.user_id, u.first_name, u.last_name, r.role_code
            FROM staff_users u JOIN roles r ON u.role_id = r.role_id
            WHERE u.is_active = 1 AND r.can_sign_final_reports = 1
            ORDER BY u.last_name
        `),
        execSQL(`SELECT exam_code, exam_name FROM exam_catalog ORDER BY exam_code`)
    ]);

    if (!Array.isArray(ordRes) || !ordRes.length) {
        setPhysBanner('phys-status-bar', 'Order not found.', true);
        return;
    }

    const o = ordRes[0];
    const study = (Array.isArray(studyRes) && studyRes.length) ? studyRes[0] : null;
    const seriesList = Array.isArray(seriesRes) ? seriesRes : [];
    const reports = Array.isArray(repRes) ? repRes : [];
    g_physTemplates = Array.isArray(tplRes) ? tplRes : [];
    g_physMacros = Array.isArray(macroRes) ? macroRes : [];
    const signers = Array.isArray(radsRes) ? radsRes : [];
    const exams = Array.isArray(examsRes) ? examsRes : [];

    const [allergies, contrasts, priors] = await Promise.all([
        execSQL(`SELECT allergen_name, severity, reaction_description FROM patient_allergies WHERE patient_id = ${o.patient_id}`),
        execSQL(`SELECT agent_name, volume_ml, route, extravasation_occurred, adverse_reaction_occurred FROM contrast_and_medication_logs WHERE order_id = ${orderId}`),
        execSQL(`
            SELECT r.report_id, o2.accession_number, ec2.exam_name, r.report_status, r.final_signed_at, r.impression_section
            FROM radiology_reports r
            JOIN imaging_orders o2 ON r.order_id = o2.order_id
            JOIN exam_catalog ec2 ON o2.exam_code = ec2.exam_code
            WHERE o2.patient_id = ${o.patient_id} AND o2.order_id != ${orderId}
            ORDER BY r.dictated_at DESC LIMIT 8
        `)
    ]);

    await logPhiAccess(o.patient_id, orderId, 'VIEW_CLINICAL_ORDER', 'Opened in Physician Reading Workspace');

    if (lockRes.ok && o.workflow_status === 'READY_FOR_READ' && g_physActiveUserId && (g_physActiveRoleCode === 'RADIOLOGIST' || g_physActiveRoleCode === 'RESIDENT_FELLOW')) {
        const assignCol = g_physActiveRoleCode === 'RESIDENT_FELLOW' ? 'assigned_resident_id' : 'assigned_radiologist_id';
        await execSQL(`
            UPDATE imaging_orders
            SET workflow_status = 'ASSIGNED_READING',
                reading_started_at = COALESCE(reading_started_at, datetime('now')),
                ${assignCol} = COALESCE(${assignCol}, ${g_physActiveUserId})
            WHERE order_id = ${orderId}
        `);
    }

    const latestRep = reports.length ? reports[0] : null;
    const isFinalized = latestRep && (latestRep.report_status === 'FINAL' || latestRep.report_status === 'ADDENDUM' || latestRep.report_status === 'CORRECTED');
    const editingDraftId = (latestRep && (latestRep.report_status === 'DRAFT' || latestRep.report_status === 'PRELIMINARY')) ? latestRep.report_id : null;
    const parentReportId = isFinalized ? latestRep.report_id : null;

    const defaultSignerId = latestRep?.signing_radiologist_id || o.assigned_radiologist_id || (signers[0]?.user_id || g_physActiveUserId);
    const signerOpts = signers.map(s =>
        `<option value="${s.user_id}" ${String(s.user_id) === String(defaultSignerId) ? 'selected' : ''}>Dr. ${s.first_name} ${s.last_name}</option>`
    ).join('');

    const tplOpts = `<option value="">-- Load Report Template --</option>` + g_physTemplates.map(t =>
        `<option value="${t.template_id}">${escAttr(t.template_name)}${t.exam_code === o.exam_code ? ' (Match)' : ''}</option>`
    ).join('');

    const macroChips = g_physMacros
        .filter(m => !m.modality_type || m.modality_type === o.modality_type)
        .slice(0, 12)
        .map(m => `<button type="button" style="padding:2px 6px; font-size:11px; margin:2px; cursor:pointer; background:#ecf0f1; border:1px solid #bdc3c7; border-radius:3px;" onclick="insertPhysMacro(${m.macro_id})">${escAttr(m.trigger_phrase)}</button>`)
        .join('');

    const seriesHtml = seriesList.length
        ? seriesList.map(s => `<div style="padding:5px 8px; border-bottom:1px solid #eee; font-size:12px;"><b>#${s.series_number} [${s.modality}]</b> ${escAttr(s.series_description || s.protocol_name || 'Series')} <span style="color:#7f8c8d;">(${s.instance_count} imgs)</span></div>`).join('')
        : '<div style="padding:8px; font-size:12px; color:#7f8c8d;">No DICOM series metadata registered yet.</div>';

    const doseHtml = study
        ? `<div style="display:flex; flex-wrap:wrap; gap:10px; font-size:11px; color:#2c3e50; background:#f8f9fa; padding:8px; border-radius:4px; margin-top:8px;">
            ${study.kvp ? `<span><b>kVp:</b> ${study.kvp}</span>` : ''}
            ${study.mas ? `<span><b>mAs:</b> ${study.mas}</span>` : ''}
            ${study.ctdi_vol_mgy ? `<span><b>CTDIvol:</b> ${study.ctdi_vol_mgy} mGy</span>` : ''}
            ${study.dlp_mgy_cm ? `<span><b>DLP:</b> ${study.dlp_mgy_cm} mGy&middot;cm</span>` : ''}
            ${study.effective_dose_msv ? `<span><b>Eff. Dose:</b> ${study.effective_dose_msv} mSv</span>` : ''}
            ${study.dose_exceeded_diagnostic_ref_level ? `<span style="color:#c0392b; font-weight:bold;">EXCEEDED DRL</span>` : ''}
           </div>`
        : '';

    const priorHtml = (Array.isArray(priors) && priors.length)
        ? priors.map(pr => `<div style="padding:6px; border-bottom:1px solid #eee; font-size:11px;">
            <b>${escAttr(pr.accession_number)}</b> - ${escAttr(pr.exam_name)} (${pr.report_status})<br>
            <span style="color:#555;">${escAttr((pr.impression_section || '').substring(0, 140))}</span>
          </div>`).join('')
        : '<div style="padding:6px; font-size:12px; color:#7f8c8d;">No prior radiology reports on file.</div>';

    const allergyItems = [];
    if (o.allergies) allergyItems.push(o.allergies);
    if (o.contrast_allergy_flag) allergyItems.push(`CONTRAST ALLERGY: ${o.contrast_allergy_details || 'Flagged'}`);
    if (Array.isArray(allergies)) allergies.forEach(a => allergyItems.push(`${a.allergen_name} (${a.severity}: ${a.reaction_description})`));

    const contrastHtml = (Array.isArray(contrasts) && contrasts.length)
        ? contrasts.map(c => `${c.agent_name} ${c.volume_ml}mL (${c.route})${c.adverse_reaction_occurred ? ' [REACTION]' : ''}`).join('; ')
        : 'None logged';

    const existingReportsHtml = reports.length
        ? reports.map(r => `
            <div style="border:1px solid #ddd; border-left:4px solid ${r.report_status === 'FINAL' ? '#27ae60' : '#f39c12'}; border-radius:4px; padding:10px; margin-bottom:10px; background:#fafafa; font-size:12px;">
                <div style="display:flex; justify-content:space-between; margin-bottom:6px;">
                    <b>v${r.version_number} &bull; ${r.report_status}</b>
                    <span style="color:#7f8c8d;">Signed/Attending: Dr. ${escAttr(r.signer_name || 'N/A')} | ${r.final_signed_at || r.dictated_at}</span>
                </div>
                ${r.standardized_category_system && r.standardized_category_system !== 'NONE' ? `<div><b>Score:</b> ${r.standardized_category_system} ${escAttr(r.standardized_category_score || '')}</div>` : ''}
                <div><b>Technique:</b> ${escAttr(r.technique_section)}</div>
                <div style="margin-top:4px; white-space:pre-wrap;"><b>Findings:</b>\n${escAttr(r.findings_section)}</div>
                <div style="margin-top:4px; white-space:pre-wrap;"><b>Impression:</b>\n${escAttr(r.impression_section)}</div>
            </div>`).join('')
        : '';

    const valHist = !isFinalized && latestRep ? (latestRep.clinical_history_section || o.clinical_indication) : o.clinical_indication;
    const valComp = !isFinalized && latestRep ? (latestRep.comparison_studies_section || '') : (priors.length ? `${priors[0].accession_number} (${priors[0].exam_name})` : 'None available.');
    const valTech = !isFinalized && latestRep ? (latestRep.technique_section || '') : '';
    const valFind = !isFinalized && latestRep ? (latestRep.findings_section || '') : '';
    const valImpr = !isFinalized && latestRep ? (latestRep.impression_section || '') : '';
    const valCatSys = !isFinalized && latestRep ? (latestRep.standardized_category_system || 'NONE') : 'NONE';
    const valCatScore = !isFinalized && latestRep ? (latestRep.standardized_category_score || '') : '';
    const valCrit = !isFinalized && latestRep ? (latestRep.is_critical_or_unexpected || 0) : 0;
    const valAcr = !isFinalized && latestRep ? (latestRep.acr_actionable_category || 'NONE') : 'NONE';
    const valRadpeer = !isFinalized && latestRep ? (latestRep.radpeer_score || '') : '';

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Reading Workspace: ${escAttr(o.accession_number)}</h2>
        <div style="display:flex; gap:8px;">
            <button class="btn" style="background:#34495e; margin:0;" onclick="loadPhysicianQueue()">&larr; Release Lock &amp; Back</button>
        </div>
    </div>

    ${!lockRes.ok ? `<div style="margin-top:8px; padding:8px 12px; background:#fef9e7; border:1px solid #f1c40f; border-left:5px solid #e67e22; border-radius:4px; font-size:13px; font-weight:bold; color:#7d6608;">LOCK WARNING: Study is currently locked by Dr. ${escAttr(lockRes.lockedBy)}. Coordinate before signing.</div>` : ''}
    <div id="read-status-bar" style="display:none; margin-top:8px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>

    <!-- Patient & Study Banner -->
    <div style="background:#fff; border-left:5px solid ${PHYS_PRIO_COLORS[o.priority] || '#3498db'}; border-radius:4px; padding:10px 14px; margin:10px 0; box-shadow:0 1px 3px rgba(0,0,0,0.08); display:flex; flex-wrap:wrap; gap:18px; font-size:13px;">
        <div><b>${escAttr(o.last_name)}, ${escAttr(o.first_name)}</b><div style="font-size:11px; color:#555;">MRN: ${escAttr(o.mrn)} | DOB: ${o.dob} | Sex: ${o.biological_sex}</div></div>
        <div><b>${escAttr(o.exam_name)}</b><div style="font-size:11px; color:#555;">${o.modality_type} (${o.body_part}) | ${o.priority} | ${o.patient_class}</div></div>
        <div><b>Referring MD:</b> Dr. ${escAttr(o.ref_md_name)}<div style="font-size:11px; color:#555;">Phone: ${escAttr(o.ref_md_phone || 'N/A')}</div></div>
        <div><b>Clinical Indication:</b><div style="font-size:12px; color:#2c3e50;">${escAttr(o.clinical_indication)}</div></div>
    </div>

    <!-- Split Pane -->
    <div style="display:grid; grid-template-columns:minmax(360px, 1.15fr) minmax(420px, 1.25fr); gap:14px; align-items:start;">
        <!-- LEFT PANE: EMBEDDED DICOM VIEWER & CLINICAL CONTEXT -->
        <div style="display:flex; flex-direction:column; gap:12px;">
            <div id="phys-dcm-mount" style="height:480px; border-radius:4px; overflow:hidden; box-shadow:0 1px 3px rgba(0,0,0,0.15);"></div>

            <div style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
                <h4 style="margin:0 0 6px 0; color:#2c3e50; border-bottom:1px solid #eee; padding-bottom:4px;">DICOM Series &amp; Dose</h4>
                ${seriesHtml}
                ${doseHtml}
                ${study?.tech_notes_for_radiologist ? `<div style="margin-top:8px; padding:6px 8px; background:#fef9e7; border-left:3px solid #f1c40f; font-size:12px;"><b>Tech Notes:</b> ${escAttr(study.tech_notes_for_radiologist)}</div>` : ''}
            </div>

            <div style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:12px;">
                <h4 style="margin:0 0 6px 0; color:#2c3e50; border-bottom:1px solid #eee; padding-bottom:4px;">Safety, Contrast &amp; Priors</h4>
                <div><b>Allergies:</b> <span style="color:${allergyItems.length ? '#c0392b' : '#555'};">${escAttr(allergyItems.join(' | ') || 'None documented')}</span></div>
                <div style="margin-top:4px;"><b>Contrast Administered:</b> ${escAttr(contrastHtml)}</div>
                <div style="margin-top:4px;"><b>Renal Labs:</b> eGFR ${o.egfr_value ?? 'N/A'} | Creatinine ${o.creatinine_value ?? 'N/A'}</div>
                <div style="margin-top:8px; border-top:1px solid #eee; padding-top:6px;">
                    <b>Prior Studies for Comparison:</b>
                    ${priorHtml}
                </div>
            </div>
        </div>

        <!-- RIGHT PANE: STRUCTURED REPORT DOCUMENTATION -->
        <div id="phys-report-form" style="background:#fff; padding:16px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
            <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; margin-bottom:10px; border-bottom:2px solid #3498db; padding-bottom:6px;">
                <h3 style="margin:0; color:#2c3e50;">${isFinalized ? 'Create Addendum / Correction (Signed Report Immutable)' : (editingDraftId ? `Editing ${latestRep.report_status} Report #${editingDraftId}` : 'New Radiology Report')}</h3>
                <select id="rep-template" style="width:auto; font-size:12px;" onchange="applyPhysTemplate(this.value)">${tplOpts}</select>
            </div>

            ${isFinalized ? `<div style="margin-bottom:12px;"><b style="font-size:12px; color:#27ae60;">Signed Report History:</b>${existingReportsHtml}</div>` : ''}

            <!-- SQL-DRIVEN DROPDOWN REPORTING ENGINE (RSNA 18-Column Schema) -->
            <div style="background:#eef5fb; border:1px solid #aed6f1; border-radius:4px; padding:10px; margin-bottom:12px;">
                <div style="font-size:12px; font-weight:bold; color:#1b4f72; margin-bottom:8px;">Structured Dropdown Reporting Engine (SQL)</div>
                <div style="display:grid; grid-template-columns:repeat(3, 1fr); gap:8px; margin-bottom:8px;">
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Modality</label>
                        <select id="rsna-mod" onchange="onRsnaModalityChange()"></select>
                    </div>
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Anatomy</label>
                        <select id="rsna-anat" onchange="onRsnaAnatomyChange()"></select>
                    </div>
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Pathology</label>
                        <select id="rsna-path" onchange="onRsnaPathologyChange()"></select>
                    </div>
                </div>
                <div style="display:grid; grid-template-columns:1fr 1fr; gap:8px;">
                    <div id="rsna-var-wrap-0" style="display:none;">
                        <label id="rsna-var-lbl-0" style="font-size:11px; font-weight:bold;">Var 1</label>
                        <select id="rsna-var-0" onchange="generateRsnaReport()"></select>
                    </div>
                    <div id="rsna-var-wrap-1" style="display:none;">
                        <label id="rsna-var-lbl-1" style="font-size:11px; font-weight:bold;">Var 2</label>
                        <select id="rsna-var-1" onchange="generateRsnaReport()"></select>
                    </div>
                    <div id="rsna-var-wrap-2" style="display:none;">
                        <label id="rsna-var-lbl-2" style="font-size:11px; font-weight:bold;">Var 3</label>
                        <select id="rsna-var-2" onchange="generateRsnaReport()"></select>
                    </div>
                    <div id="rsna-var-wrap-3" style="display:none;">
                        <label id="rsna-var-lbl-3" style="font-size:11px; font-weight:bold;">Var 4</label>
                        <select id="rsna-var-3" onchange="generateRsnaReport()"></select>
                    </div>
                </div>
                <details style="margin-top:8px;">
                    <summary style="font-size:11px; font-weight:bold; color:#2c3e50; cursor:pointer;">Full Assembled Base Template Preview</summary>
                    <textarea id="rsna-full-preview" rows="5" style="margin-top:6px; font-family:monospace; font-size:11px;" readonly></textarea>
                    <button type="button" class="btn" style="padding:3px 8px; font-size:11px; background:#2980b9; margin-top:4px;" onclick="applyRsnaBaseToFindings()">Copy Full Base Template into Findings</button>
                </details>
            </div>

            <div id="rep-cde-container"></div>

            ${macroChips ? `<div style="margin-bottom:10px;"><span style="font-size:11px; font-weight:bold; color:#7f8c8d;">Voice / Text Macros (click to insert into Findings):</span><br>${macroChips}</div>` : ''}

            <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px;">
                <div>
                    <label style="font-size:12px; font-weight:bold;">Signing Attending Radiologist *</label>
                    <select id="rep-signer">${signerOpts}</select>
                </div>
                <div>
                    <label style="font-size:12px; font-weight:bold;">Comparison Studies</label>
                    <input type="text" id="rep-comp" value="${escAttr(valComp)}">
                </div>
            </div>

            <label style="font-size:12px; font-weight:bold;">Clinical History</label>
            <textarea id="rep-hist" rows="2" style="margin-bottom:8px;">${escAttr(valHist)}</textarea>

            <label style="font-size:12px; font-weight:bold;">Technique *</label>
            <textarea id="rep-tech" rows="2" style="margin-bottom:8px;" placeholder="Imaging protocol, contrast dose, sequences...">${escAttr(valTech)}</textarea>

            <label style="font-size:12px; font-weight:bold;">Findings *</label>
            <textarea id="rep-find" rows="7" style="margin-bottom:8px;" placeholder="Detailed anatomical findings...">${escAttr(valFind)}</textarea>

            <label style="font-size:12px; font-weight:bold;">Impression *</label>
            <textarea id="rep-impr" rows="4" style="margin-bottom:10px;" placeholder="1. Primary diagnostic impression...">${escAttr(valImpr)}</textarea>

            <!-- Structured Coding, Critical Findings & Peer Review -->
            <div style="background:#f8f9fa; border:1px solid #ddd; border-radius:4px; padding:10px; margin-bottom:12px;">
                <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(150px, 1fr)); gap:8px;">
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Category System</label>
                        <select id="rep-catsys">
                            ${['NONE','BI-RADS','LI-RADS','PI-RADS','TI-RADS','Lung-RADS','CAD-RADS'].map(c =>
                                `<option value="${c}" ${valCatSys === c ? 'selected' : ''}>${c}</option>`
                            ).join('')}
                        </select>
                    </div>
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Category Score</label>
                        <input type="text" id="rep-catscore" value="${escAttr(valCatScore)}" placeholder="e.g. 4A, 5">
                    </div>
                    <div>
                        <label style="font-size:11px; font-weight:bold;">RADPEER Score</label>
                        <select id="rep-radpeer">
                            <option value="">-- None --</option>
                            <option value="1_CONCUR" ${valRadpeer === '1_CONCUR' ? 'selected' : ''}>1_CONCUR</option>
                            <option value="2A_MINOR_DISCREPANCY" ${valRadpeer === '2A_MINOR_DISCREPANCY' ? 'selected' : ''}>2A_MINOR_DISCREPANCY</option>
                            <option value="2B_MAJOR_DISCREPANCY" ${valRadpeer === '2B_MAJOR_DISCREPANCY' ? 'selected' : ''}>2B_MAJOR_DISCREPANCY</option>
                        </select>
                    </div>
                </div>

                <div style="margin-top:10px; padding-top:8px; border-top:1px solid #e0e0e0; display:flex; flex-wrap:wrap; gap:12px; align-items:center;">
                    <label style="font-size:12px; font-weight:bold; color:#c0392b; display:inline-flex; align-items:center; gap:5px; cursor:pointer;">
                        <input type="checkbox" id="rep-critical" style="width:auto;" ${valCrit ? 'checked' : ''} onchange="document.getElementById('rep-acr').disabled = !this.checked;">
                        Critical / Unexpected Finding (Auto-Escalation)
                    </label>
                    <select id="rep-acr" style="width:auto;" ${valCrit ? '' : 'disabled'}>
                        <option value="NONE" ${valAcr === 'NONE' ? 'selected' : ''}>NONE</option>
                        <option value="CAT1_IMMEDIATE_LIFE_THREAT_1HR" ${valAcr === 'CAT1_IMMEDIATE_LIFE_THREAT_1HR' ? 'selected' : ''}>CAT1: Immediate Life Threat (1 hr SLA)</option>
                        <option value="CAT2_URGENT_WITHIN_24HR" ${valAcr === 'CAT2_URGENT_WITHIN_24HR' ? 'selected' : ''}>CAT2: Urgent (24 hr SLA)</option>
                        <option value="CAT3_INCIDENTAL_MALIGNANCY_72HR" ${valAcr === 'CAT3_INCIDENTAL_MALIGNANCY_72HR' ? 'selected' : ''}>CAT3: Actionable / Incidental (72 hr SLA)</option>
                    </select>
                </div>

                <details style="margin-top:8px;">
                    <summary style="font-size:12px; font-weight:bold; color:#2c3e50; cursor:pointer;">+ Optional Clinical Follow-Up Recall</summary>
                    <div style="display:grid; grid-template-columns:1fr 1fr 1fr; gap:8px; margin-top:8px;">
                        <div>
                            <label style="font-size:11px;">Recommended Exam</label>
                            <select id="rep-recall-exam">
                                <option value="">-- None --</option>
                                ${exams.map(e => `<option value="${escAttr(e.exam_code)}">${escAttr(e.exam_code)} - ${escAttr(e.exam_name)}</option>`).join('')}
                            </select>
                        </div>
                        <div>
                            <label style="font-size:11px;">Due Date</label>
                            <input type="date" id="rep-recall-due">
                        </div>
                        <div>
                            <label style="font-size:11px;">Recall Reason</label>
                            <input type="text" id="rep-recall-reason" placeholder="e.g. 6-month nodule follow-up">
                        </div>
                    </div>
                </details>
            </div>

            <!-- Action Buttons -->
            <div style="display:flex; flex-wrap:wrap; gap:8px;">
                ${isFinalized
                    ? `<button type="button" class="btn" style="background:#8e44ad;" onclick="previewPhysPaperReport(${o.order_id}, ${latestRep?.report_id || 0})">Preview Paper Report (Typst)</button>
                       <button class="btn" style="background:#8e44ad;" onclick="savePhysReport(${o.order_id}, ${o.patient_id}, 'ADDENDUM', null, ${parentReportId}, ${(latestRep.version_number || 1) + 1})">Sign Addendum</button>
                       <button class="btn" style="background:#d35400;" onclick="savePhysReport(${o.order_id}, ${o.patient_id}, 'CORRECTED', null, ${parentReportId}, ${(latestRep.version_number || 1) + 1})">Sign Corrected Report</button>`
                    : `<button class="btn" style="background:#7f8c8d;" onclick="savePhysReport(${o.order_id}, ${o.patient_id}, 'DRAFT', ${editingDraftId || 'null'}, null, 1)">Save Draft</button>
                       <button type="button" class="btn" style="background:#8e44ad;" onclick="previewPhysPaperReport(${o.order_id}, ${latestRep?.report_id || 0})">Preview Paper Report (Typst)</button>
                       <button class="btn" style="background:#f39c12;" onclick="savePhysReport(${o.order_id}, ${o.patient_id}, 'PRELIMINARY', ${editingDraftId || 'null'}, null, 1)">Release Preliminary</button>
                       <button class="btn" style="background:#27ae60;" onclick="savePhysReport(${o.order_id}, ${o.patient_id}, 'FINAL', ${editingDraftId || 'null'}, null, 1)">Sign Final Report</button>`
                }
            </div>
        </div>
    </div>`;

    await initRsnaDropdownEngine(o.modality_type);

    if (window.DicomViewer) {
        DicomViewer.mount('phys-dcm-mount', o.accession_number, {
            seriesCount: study ? study.total_series_count : 0,
            instanceCount: study ? study.total_instance_count : 0
        });
    }

    const matchTpl = g_physTemplates.find(t => t.exam_code === o.exam_code);
    if (matchTpl) await loadTemplateCdes(matchTpl.template_id, editingDraftId);
}

function applyPhysTemplate(tplId) {
    if (!tplId) return;
    const t = g_physTemplates.find(x => String(x.template_id) === String(tplId));
    if (!t) return;
    document.getElementById('rep-tech').value = t.technique_boilerplate || '';
    document.getElementById('rep-find').value = t.structured_findings_template || '';
    document.getElementById('rep-impr').value = t.normal_impression_boilerplate || '';
    loadTemplateCdes(tplId);
}

function insertPhysMacro(macroId) {
    const m = g_physMacros.find(x => String(x.macro_id) === String(macroId));
    const ta = document.getElementById('rep-find');
    if (!m || !ta) return;
    const start = ta.selectionStart ?? ta.value.length;
    const end = ta.selectionEnd ?? ta.value.length;
    ta.value = ta.value.slice(0, start) + m.expansion_text + ta.value.slice(end);
    ta.focus();
}

async function savePhysReport(orderId, patientId, targetStatus, existingReportId, parentReportId, versionNum) {
    const signerId = document.getElementById('rep-signer')?.value;
    const hist = (document.getElementById('rep-hist')?.value || '').trim();
    const comp = (document.getElementById('rep-comp')?.value || '').trim();
    const tech = (document.getElementById('rep-tech')?.value || '').trim();
    const find = (document.getElementById('rep-find')?.value || '').trim();
    const impr = (document.getElementById('rep-impr')?.value || '').trim();
    const catSys = document.getElementById('rep-catsys')?.value || 'NONE';
    const catScore = (document.getElementById('rep-catscore')?.value || '').trim();
    const radpeer = document.getElementById('rep-radpeer')?.value || '';
    const isCrit = document.getElementById('rep-critical')?.checked ? 1 : 0;
    const acrCat = isCrit ? (document.getElementById('rep-acr')?.value || 'CAT3_INCIDENTAL_MALIGNANCY_72HR') : 'NONE';

    if (!signerId) return setPhysBanner('read-status-bar', 'Select a signing attending radiologist.', true);
    if (!tech || !find || !impr) return setPhysBanner('read-status-bar', 'Technique, Findings, and Impression are required.', true);

    const residentId = g_physActiveRoleCode === 'RESIDENT_FELLOW' ? g_physActiveUserId : 'NULL';
    const peerId = radpeer && g_physActiveUserId ? g_physActiveUserId : 'NULL';
    const prelimTs = targetStatus === 'PRELIMINARY' ? "datetime('now')" : 'NULL';
    const finalTs = (targetStatus === 'FINAL' || targetStatus === 'ADDENDUM' || targetStatus === 'CORRECTED') ? "datetime('now')" : 'NULL';

    let res;
    let savedReportId = existingReportId;

    if (existingReportId) {
        const prevRes = await execSQL(`SELECT report_status FROM radiology_reports WHERE report_id = ${existingReportId}`);
        const prevStatus = Array.isArray(prevRes) && prevRes[0]?.report_status;

        res = await execSQL(`
            UPDATE radiology_reports
            SET signing_radiologist_id = ${signerId},
                report_status = '${targetStatus}',
                clinical_history_section = ${sqlStr(hist, true)},
                comparison_studies_section = ${sqlStr(comp, true)},
                technique_section = ${sqlStr(tech)},
                findings_section = ${sqlStr(find)},
                impression_section = ${sqlStr(impr)},
                standardized_category_system = '${catSys}',
                standardized_category_score = ${sqlStr(catScore, true)},
                is_critical_or_unexpected = ${isCrit},
                acr_actionable_category = '${acrCat}',
                radpeer_score = ${sqlStr(radpeer, true)},
                peer_reviewed_by_id = ${peerId},
                preliminary_released_at = COALESCE(preliminary_released_at, ${prelimTs}),
                final_signed_at = COALESCE(final_signed_at, ${finalTs})
            WHERE report_id = ${existingReportId}
        `);
        if (res && res.error) return setPhysBanner('read-status-bar', res.error, true);

        if (prevStatus === 'PRELIMINARY' && targetStatus === 'FINAL') {
            await execSQL(`UPDATE imaging_orders SET workflow_status = 'FINAL_REPORTED', final_reported_at = datetime('now') WHERE order_id = ${orderId}`);
        }
    } else {
        res = await execSQL(`
            INSERT INTO radiology_reports (
                order_id, version_number, parent_report_id, drafted_by_resident_id, signing_radiologist_id,
                report_status, clinical_history_section, comparison_studies_section, technique_section,
                findings_section, impression_section, standardized_category_system, standardized_category_score,
                is_critical_or_unexpected, acr_actionable_category, radpeer_score, peer_reviewed_by_id,
                preliminary_released_at, final_signed_at
            ) VALUES (
                ${orderId}, ${versionNum || 1}, ${parentReportId || 'NULL'}, ${residentId}, ${signerId},
                '${targetStatus}', ${sqlStr(hist, true)}, ${sqlStr(comp, true)}, ${sqlStr(tech)},
                ${sqlStr(find)}, ${sqlStr(impr)}, '${catSys}', ${sqlStr(catScore, true)},
                ${isCrit}, '${acrCat}', ${sqlStr(radpeer, true)}, ${peerId},
                ${prelimTs}, ${finalTs}
            )
        `);
        if (res && res.error) return setPhysBanner('read-status-bar', res.error, true);

        const idFetch = await execSQL(`SELECT MAX(report_id) AS id FROM radiology_reports WHERE order_id = ${orderId}`);
        savedReportId = Array.isArray(idFetch) && idFetch[0]?.id ? idFetch[0].id : null;
    }

    await saveTemplateCdes(savedReportId);

if (savedReportId) {
        await fetch(`/api/report_typ?report_id=${encodeURIComponent(savedReportId)}&regen=1`).catch(() => {});
    }

    const recReason = (document.getElementById('rep-recall-reason')?.value || '').trim();
    const recDue = (document.getElementById('rep-recall-due')?.value || '').trim();
    const recExam = document.getElementById('rep-recall-exam')?.value || '';
    if (savedReportId && recReason && recDue) {
        await execSQL(`
            INSERT INTO clinical_followup_recalls (source_report_id, patient_id, recommended_exam_code, recall_reason, due_date)
            VALUES (${savedReportId}, ${patientId}, ${sqlStr(recExam, true)}, ${sqlStr(recReason)}, '${recDue}')
        `);
    }

    await logPhiAccess(patientId, orderId, 'VIEW_REPORT', `Saved radiology report as ${targetStatus}`);

    if (targetStatus === 'DRAFT') {
        setPhysBanner('read-status-bar', `Draft saved (Report #${savedReportId}).`);
    } else {
        await releasePhysLock(orderId);
        g_physActiveOrderId = null;
        await loadPhysicianQueue();
        setPhysBanner('phys-status-bar', `Report #${savedReportId} saved as ${targetStatus}. Outbound dispatch and escalations updated.`);
    }
}
/* ============================================================================
 * 3. CRITICAL FINDINGS ESCALATION ACKNOWLEDGMENT VIEW
 * ============================================================================ */
async function loadCriticalAlertsView() {
    if (g_physActiveOrderId) {
        await releasePhysLock(g_physActiveOrderId);
        g_physActiveOrderId = null;
    }

    const alerts = await execSQL(`
        SELECT cfe.escalation_id, cfe.order_id, cfe.alert_tier, cfe.sla_deadline_at, cfe.initiated_at,
               o.accession_number, p.first_name || ' ' || p.last_name AS patient_name,
               ref.first_name || ' ' || ref.last_name AS target_md,
               COALESCE(ref.mobile_or_pager, ref.work_phone, 'N/A') AS md_contact,
               r.impression_section,
               CAST((julianday(cfe.sla_deadline_at) - julianday('now')) * 1440 AS INTEGER) AS mins_left
        FROM critical_findings_escalations cfe
        JOIN imaging_orders o ON cfe.order_id = o.order_id
        JOIN patients p ON o.patient_id = p.patient_id
        JOIN radiology_reports r ON cfe.report_id = r.report_id
        JOIN staff_users ref ON cfe.target_provider_id = ref.user_id
        WHERE cfe.escalation_status != 'ACKNOWLEDGED_CLOSED'
        ORDER BY cfe.sla_deadline_at ASC
    `);

    let bodyHtml = '';
    if (!Array.isArray(alerts) || alerts.length === 0) {
        bodyHtml = '<p style="background:#fff; padding:16px; border-radius:4px;">No open critical finding escalations.</p>';
    } else {
        bodyHtml = alerts.map(a => `
        <div style="background:#fff; border:1px solid #e74c3c; border-left:5px solid #c0392b; border-radius:4px; padding:14px; margin-bottom:12px; font-size:13px;">
            <div style="display:flex; justify-content:space-between; flex-wrap:wrap; gap:8px;">
                <div><b>${a.alert_tier}</b> &bull; Accession: <b>${escAttr(a.accession_number)}</b> &bull; Patient: <b>${escAttr(a.patient_name)}</b></div>
                <div style="color:${a.mins_left < 0 ? '#c0392b' : '#d35400'}; font-weight:bold;">SLA Deadline: ${a.sla_deadline_at} (${a.mins_left} mins)</div>
            </div>
            <div style="margin:6px 0; color:#555;">Target Provider: <b>Dr. ${escAttr(a.target_md)}</b> (${escAttr(a.md_contact)})</div>
            <div style="background:#fdedec; padding:8px; border-radius:4px; margin:8px 0;"><b>Critical Impression:</b> ${escAttr(a.impression_section)}</div>
            <div style="display:flex; flex-wrap:wrap; gap:8px; align-items:center;">
                <input type="text" id="ack-name-${a.escalation_id}" placeholder="Acknowledged by Name &amp; Role (e.g. Dr. Pendelton, ER MD)" style="flex:1; min-width:220px;">
                <select id="ack-method-${a.escalation_id}" style="width:auto;">
                    <option value="DIRECT_PHONE_CALL">DIRECT_PHONE_CALL</option>
                    <option value="IN_PERSON_VERBAL">IN_PERSON_VERBAL</option>
                    <option value="SECURE_BIDIRECTIONAL_SMS_ACK">SECURE_BIDIRECTIONAL_SMS_ACK</option>
                </select>
                <label style="font-size:12px; display:inline-flex; align-items:center; gap:4px; cursor:pointer;">
                    <input type="checkbox" id="ack-rb-${a.escalation_id}" style="width:auto;" checked> Verbatim Read-Back Verified
                </label>
                <button class="btn" style="background:#27ae60; margin:0;" onclick="acknowledgeCriticalAlert(${a.escalation_id})">Close Escalation</button>
            </div>
        </div>`).join('');
    }

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Unacknowledged Critical Findings (Closed-Loop SLA)</h2>
        <button class="btn" style="background:#34495e; margin:0;" onclick="loadPhysicianQueue()">&larr; Back to Reading Queue</button>
    </div>
    <div id="crit-status-bar" style="display:none; margin:10px 0; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div style="margin-top:12px;">${bodyHtml}</div>`;
}

async function acknowledgeCriticalAlert(escalationId) {
    const nameRole = (document.getElementById(`ack-name-${escalationId}`)?.value || '').trim();
    const method = document.getElementById(`ack-method-${escalationId}`)?.value || 'DIRECT_PHONE_CALL';
    const rb = document.getElementById(`ack-rb-${escalationId}`)?.checked ? 1 : 0;

    if (!nameRole || !rb) {
        return setPhysBanner('crit-status-bar', 'Enter acknowledging clinician name/role and verify verbatim read-back.', true);
    }

    const res = await execSQL(`
        UPDATE critical_findings_escalations
        SET escalation_status = 'ACKNOWLEDGED_CLOSED',
            acknowledged_by_provider_id = ${g_physActiveUserId || 'NULL'},
            acknowledged_by_name_and_role = ${sqlStr(nameRole)},
            communication_method = '${method}',
            verbatim_readback_verified = 1,
            acknowledged_at = datetime('now')
        WHERE escalation_id = ${escalationId}
    `);
    if (res && res.error) return setPhysBanner('crit-status-bar', res.error, true);
    loadCriticalAlertsView();
}