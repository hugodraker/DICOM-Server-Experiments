/* ============================================================================
 * RIS Technologist, Nurse & Waiting Room Console
 * (Check-In & Queue Tickets, Pre-Scan Safety Gate, & Scanner Protocoling)
 * ============================================================================ */

let g_techFilterTimer = null;
let g_techActiveUserId = null;

const TECH_STATUS_FLOW = {
    SCHEDULED:          { next: 'ARRIVED_WAITING',    label: 'Check In',        bg: '#2980b9' },
    ARRIVED_WAITING:    { next: 'IN_PREP_SCREENING',  label: 'Start Prep',      bg: '#8e44ad' },
    IN_PREP_SCREENING:  { next: 'READY_FOR_SCAN',     label: 'Ready for Scan',  bg: '#16a085' },
    READY_FOR_SCAN:     { next: 'IN_PROGRESS',        label: 'Start Scan',      bg: '#e67e22' },
    IN_PROGRESS:        { next: 'IMAGES_CAPTURED_QA', label: 'Complete Scan',   bg: '#27ae60' },
    IMAGES_CAPTURED_QA: { next: 'READY_FOR_READ',     label: 'Verify QA -> Read', bg: '#2ecc71' }
};

function addUnlinkedTagRow(tag = '', vr = 'LO', val = '') {
    const wrap = document.getElementById('unlk-tag-rows');
    if (!wrap) return;
    const row = document.createElement('div');
    row.className = 'unlk-tag-row';
    row.style.cssText = 'display:grid; grid-template-columns:150px 70px 1fr auto; gap:6px; align-items:center;';
    row.innerHTML = `
        <input type="text" class="ut-tag" value="${escAttr(tag)}" placeholder="0x0018,0x0015" style="font-family:monospace; font-size:12px;">
        <input type="text" class="ut-vr"  value="${escAttr(vr)}"  placeholder="LO" maxlength="2" style="font-family:monospace; font-size:12px; text-transform:uppercase;">
        <input type="text" class="ut-val" value="${escAttr(val)}" placeholder="Tag value (added if missing from image)" style="font-size:12px;">
        <button type="button" class="btn btn-danger" style="margin:0; padding:3px 8px; font-size:11px;" onclick="this.parentElement.remove()">X</button>`;
    wrap.appendChild(row);
}

function addPresetUnlinkedTag() {
    const raw = document.getElementById('unlk-tag-preset')?.value;
    if (!raw) return;
    const [tag, vr] = raw.split('|');
    addUnlinkedTagRow(tag, vr, '');
}

function filterUnlinkedOrders() {
    const q = (document.getElementById('unlk-ord-filter')?.value || '').trim().toLowerCase();
    const sel = document.getElementById('unlk-existing-ord');
    if (!sel) return;
    const prevVal = sel.value;

    const filtered = q
        ? g_unlkOrders.filter(o =>
            `${o.accession_number} ${o.pat_name} ${o.mrn} ${o.exam_name} ${o.modality_type}`.toLowerCase().includes(q))
        : g_unlkOrders;

    sel.innerHTML = `<option value="">-- Select Existing RIS Order (${filtered.length}) --</option>` + filtered.map(o =>
        `<option value="${o.order_id}">${escAttr(o.accession_number)} | ${escAttr(o.pat_name)} (${escAttr(o.mrn)}) - ${escAttr(o.exam_name)}</option>`
    ).join('');

    if (prevVal && filtered.some(o => String(o.order_id) === String(prevVal))) {
        sel.value = prevVal;
    } else if (q && filtered.length > 0) {
        sel.value = String(filtered[0].order_id);
        onUnlinkedOrderSelected();
    }
}

async function loadUnlinkedDicomQueue() {
    await fetch('/api/sync_worklist', { method: 'POST' }).catch(() => {});

    const [items, orders, patients, exams, refDocs] = await Promise.all([
        execSQL(`SELECT * FROM dicom_unlinked_queue WHERE status = 'UNLINKED' ORDER BY received_at DESC LIMIT 200`),
        execSQL(`
            SELECT o.order_id, o.accession_number, o.referring_md_id, p.mrn,
                   p.last_name || ', ' || p.first_name AS pat_name, ec.exam_name, ec.modality_type
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            WHERE o.workflow_status NOT IN ('CANCELLED', 'NO_SHOW')
            ORDER BY o.order_id DESC LIMIT 300
        `),
        execSQL(`SELECT patient_id, mrn, last_name, first_name, dob, biological_sex FROM patients ORDER BY last_name, first_name LIMIT 300`),
        execSQL(`SELECT exam_code, exam_name, modality_type FROM exam_catalog ORDER BY modality_type, exam_name`),
        execSQL(`
            SELECT u.user_id, u.first_name, u.last_name,
                   COALESCE(u.preferred_comm_channel, 'HL7_ORU_R01') AS preferred_comm_channel,
                   COALESCE(u.fax_number, '') AS fax_number,
                   COALESCE(u.email, '') AS email
            FROM staff_users u JOIN roles r ON u.role_id = r.role_id
            WHERE u.is_active = 1 AND r.role_code IN ('REFERRING_MD', 'RADIOLOGIST', 'RESIDENT_FELLOW')
            ORDER BY CASE r.role_code WHEN 'REFERRING_MD' THEN 1 ELSE 2 END, u.last_name
        `)
    ]);

    g_unlkOrders = Array.isArray(orders) ? orders : [];
    g_unlkRefDocs = Array.isArray(refDocs) ? refDocs : [];

    const patOpts = `<option value="NEW">+ Create New Patient Below</option>` + (Array.isArray(patients) ? patients : []).map(p =>
        `<option value="${p.patient_id}">${escAttr(p.last_name)}, ${escAttr(p.first_name)} (${escAttr(p.mrn)} | DOB: ${p.dob})</option>`
    ).join('');

    const examOpts = (Array.isArray(exams) ? exams : []).map(e =>
        `<option value="${escAttr(e.exam_code)}">[${escAttr(e.modality_type)}] ${escAttr(e.exam_code)} - ${escAttr(e.exam_name)}</option>`
    ).join('');

    const refOpts = `<option value="">-- Keep Current Referring MD --</option>` + g_unlkRefDocs.map(d =>
        `<option value="${d.user_id}">Dr. ${escAttr(d.first_name)} ${escAttr(d.last_name)} [${escAttr(d.preferred_comm_channel)}]</option>`
    ).join('');

    let rowsHtml = '';
    if (items && items.error) {
        rowsHtml = `<tr><td colspan="5" style="padding:14px; color:#c0392b; font-weight:bold;">SQL Error: ${escAttr(items.error)}</td></tr>`;
    } else if (Array.isArray(items) && items.length) {
        rowsHtml = items.map(it => `
        <tr>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; text-align:center;">
                <input type="checkbox" class="unlk-chk" value="${it.queue_id}" checked style="width:auto;">
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">${escAttr(it.received_at)}</td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;"><b>${escAttr(it.modality || 'OT')}</b></td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; font-size:12px;">
                Acc: <b>${escAttr(it.raw_accession || '(blank)')}</b> &nbsp;|&nbsp;
                Pat: ${escAttr(it.raw_patient_name || '(blank)')} (${escAttr(it.raw_patient_id || '-')})
                <div style="font-size:10px; color:#7f8c8d;">SOP: ${escAttr(it.sop_instance_uid)}</div>
            </td>
            <td style="padding:6px; border-bottom:1px solid #ecf0f1; white-space:nowrap;">
                <button class="btn" style="background:#2980b9; padding:3px 8px; font-size:11px; margin:0;" onclick="previewUnlinkedSop('${escAttr(it.sop_instance_uid)}')">Preview</button>
            </td>
        </tr>`).join('');
    } else {
        rowsHtml = `<tr><td colspan="5" style="padding:14px;">No unlinked DICOM images in staging queue. Use Upload Image(s) or Upload Folder above.</td></tr>`;
    }

    const todayTag = new Date().toISOString().slice(0, 10).replace(/-/g, '');
    const defaultAcc = `ACC-${todayTag}-${String(Date.now()).slice(-4)}`;

    document.getElementById('main-content').innerHTML = `
    <div class="header-flex">
        <h2>Unlinked DICOM Reconciliation &amp; Manual Upload</h2>
        <div style="display:flex; gap:8px; flex-wrap:wrap;">
            <button class="btn" style="background:#27ae60; margin:0;" onclick="document.getElementById('unlk-file-in').click()">+ Upload Image(s)</button>
            <input type="file" id="unlk-file-in" multiple style="display:none;" onchange="uploadUnlinkedFiles(this.files); this.value='';">
            <button class="btn" style="background:#8e44ad; margin:0;" onclick="document.getElementById('unlk-dir-in').click()">+ Upload Folder</button>
            <input type="file" id="unlk-dir-in" webkitdirectory directory multiple style="display:none;" onchange="uploadUnlinkedFiles(this.files); this.value='';">
            <button class="btn" style="background:#34495e; margin:0;" onclick="loadTechConsole()">&larr; Back to Tech Console</button>
        </div>
    </div>

    <div id="unlk-status-bar" style="display:none; margin:10px 0; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>

    <!-- STAGING TABLE -->
    <div style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-top:10px; max-height:260px; overflow-y:auto;">
        <table style="width:100%; border-collapse:collapse;">
            <thead><tr style="background:#2c3e50; color:#fff; font-size:12px;">
                <th style="padding:6px; width:36px; text-align:center;">
                    <input type="checkbox" checked style="width:auto;" onchange="document.querySelectorAll('.unlk-chk').forEach(c => c.checked = this.checked)">
                </th>
                <th style="padding:6px; text-align:left;">Received</th>
                <th style="padding:6px; text-align:left;">Mod</th>
                <th style="padding:6px; text-align:left;">Incoming DICOM Header</th>
                <th style="padding:6px; text-align:left;">Preview</th>
            </tr></thead>
            <tbody>${rowsHtml}</tbody>
        </table>
    </div>

    <!-- ASSIGN / CREATE PATIENT & EXAM + REFERRING MD / DESTINATION + TAG OVERRIDES -->
    <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-top:12px; font-size:13px;">
        <div style="display:flex; gap:16px; align-items:center; border-bottom:2px solid #3498db; padding-bottom:8px; margin-bottom:12px; flex-wrap:wrap;">
            <b style="color:#2c3e50;">Assign Selected Images &amp; Rewrite DICOM Headers:</b>
            <label style="cursor:pointer; font-weight:bold;">
                <input type="radio" name="unlk-mode" value="EXISTING" checked style="width:auto;" onchange="toggleUnlinkedAssignMode()"> Link to Existing Order
            </label>
            <label style="cursor:pointer; font-weight:bold;">
                <input type="radio" name="unlk-mode" value="NEW_ORDER" style="width:auto;" onchange="toggleUnlinkedAssignMode()"> Create New Patient / Exam Order
            </label>
        </div>

        <!-- FILTERABLE TARGET RIS ORDER -->
        <div id="unlk-mode-existing" style="margin-bottom:10px;">
            <label style="font-size:12px; font-weight:bold;">Filter &amp; Select Target RIS Order *</label>
            <div style="display:grid; grid-template-columns:1fr 2fr; gap:8px;">
                <input type="text" id="unlk-ord-filter" placeholder="Type accession, patient, MRN, exam..." oninput="filterUnlinkedOrders()">
                <select id="unlk-existing-ord" onchange="onUnlinkedOrderSelected()"></select>
            </div>
        </div>

        <!-- CREATE NEW PATIENT / ORDER -->
        <div id="unlk-mode-new" style="display:none; margin-bottom:10px;">
            <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px;">
                <div>
                    <label style="font-size:12px; font-weight:bold;">Patient *</label>
                    <select id="unlk-pat-sel" onchange="document.getElementById('unlk-new-pat-fields').style.display = this.value === 'NEW' ? 'grid' : 'none';">${patOpts}</select>
                </div>
                <div>
                    <label style="font-size:12px; font-weight:bold;">Exam / Procedure *</label>
                    <select id="unlk-exam-sel">${examOpts}</select>
                </div>
            </div>

            <div id="unlk-new-pat-fields" style="display:grid; grid-template-columns:1fr 1fr 1fr 0.7fr 1fr; gap:8px; background:#f8f9fa; border:1px solid #ddd; padding:10px; border-radius:4px; margin-bottom:10px;">
                <div><label style="font-size:11px; font-weight:bold;">Last Name *</label><input type="text" id="unlk-np-last" placeholder="Smith"></div>
                <div><label style="font-size:11px; font-weight:bold;">First Name *</label><input type="text" id="unlk-np-first" placeholder="Jane"></div>
                <div><label style="font-size:11px; font-weight:bold;">DOB *</label><input type="date" id="unlk-np-dob" value="1980-01-01"></div>
                <div><label style="font-size:11px; font-weight:bold;">Sex</label>
                    <select id="unlk-np-sex"><option value="F">F</option><option value="M">M</option><option value="O">O</option></select>
                </div>
                <div><label style="font-size:11px; font-weight:bold;">MRN (Auto if blank)</label><input type="text" id="unlk-np-mrn" placeholder="MRN-..."></div>
            </div>

            <div style="display:grid; grid-template-columns:1fr 0.8fr 1.8fr; gap:10px;">
                <div><label style="font-size:12px; font-weight:bold;">Accession # *</label><input type="text" id="unlk-ord-acc" value="${defaultAcc}"></div>
                <div><label style="font-size:12px; font-weight:bold;">Priority</label>
                    <select id="unlk-ord-prio">
                        <option value="ROUTINE">ROUTINE</option>
                        <option value="URGENT">URGENT</option>
                        <option value="STAT">STAT</option>
                    </select>
                </div>
                <div><label style="font-size:12px; font-weight:bold;">Clinical Indication *</label><input type="text" id="unlk-ord-ind" value="Manual DICOM import / outside study"></div>
            </div>
        </div>

        <!-- REFERRING MD & REPORT DESTINATION OVERRIDE (SHARED FOR BOTH MODES) -->
        <div style="display:grid; grid-template-columns:1.2fr 1fr 1.3fr; gap:10px; background:#eef5fb; border:1px solid #aed6f1; padding:10px; border-radius:4px; margin-bottom:12px;">
            <div>
                <label style="font-size:12px; font-weight:bold;">Referring MD (Change / Override)</label>
                <select id="unlk-ref-md" onchange="onUnlinkedRefMdChanged()">${refOpts}</select>
            </div>
            <div>
                <label style="font-size:12px; font-weight:bold;">Report Destination Channel</label>
                <select id="unlk-dest-ch">
                    <option value="">-- Keep Default --</option>
                    <option value="FAX">FAX</option>
                    <option value="HL7_ORU_R01">HL7_ORU_R01</option>
                    <option value="HL7_FHIR_PUSH">HL7_FHIR_PUSH</option>
                    <option value="EMAIL">EMAIL</option>
                    <option value="SMS">SMS</option>
                    <option value="PATIENT_PORTAL">PATIENT_PORTAL</option>
                </select>
            </div>
            <div>
                <label style="font-size:12px; font-weight:bold;">Destination Endpoint (Fax / Email / HL7)</label>
                <input type="text" id="unlk-dest-ep" placeholder="e.g. 416-555-4400 or dr@clinic.ca">
            </div>
        </div>

        <!-- ARBITRARY DICOM TAG OVERRIDE & INSERT MISSING TAGS -->
        <details style="background:#fafafa; border:1px solid #ddd; border-radius:4px; padding:10px; margin-bottom:12px;">
            <summary style="font-size:12px; font-weight:bold; color:#2c3e50; cursor:pointer;">Advanced DICOM Header Tag Overrides (Edit Any Tag / Add Missing Tags)</summary>
            <div style="display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin:8px 0;">
                <select id="unlk-tag-preset" style="width:auto; min-width:250px; font-size:12px;">
                    <option value="">-- Quick-Add Common Tag --</option>
                    <option value="0x0008,0x0080|LO">0x0008,0x0080 - Institution Name (LO)</option>
                    <option value="0x0008,0x103E|LO">0x0008,0x103E - Series Description (LO)</option>
                    <option value="0x0018,0x0015|CS">0x0018,0x0015 - Body Part Examined (CS)</option>
                    <option value="0x0020,0x0060|CS">0x0020,0x0060 - Laterality L/R (CS)</option>
                    <option value="0x0008,0x0020|DA">0x0008,0x0020 - Study Date YYYYMMDD (DA)</option>
                    <option value="0x0008,0x0030|TM">0x0008,0x0030 - Study Time HHMMSS (TM)</option>
                    <option value="0x0010,0x1010|AS">0x0010,0x1010 - Patient Age e.g. 045Y (AS)</option>
                    <option value="0x0008,0x0070|LO">0x0008,0x0070 - Manufacturer (LO)</option>
                    <option value="0x0008,0x1090|LO">0x0008,0x1090 - Manufacturer Model (LO)</option>
                    <option value="0x0008,0x1070|PN">0x0008,0x1070 - Operator's Name (PN)</option>
                </select>
                <button type="button" class="btn" style="background:#34495e; margin:0; padding:4px 10px; font-size:12px;" onclick="addPresetUnlinkedTag()">+ Add Preset</button>
                <button type="button" class="btn" style="background:#2980b9; margin:0; padding:4px 10px; font-size:12px;" onclick="addUnlinkedTagRow()">+ Add Custom Tag</button>
            </div>
            <div id="unlk-tag-rows" style="display:flex; flex-direction:column; gap:6px;"></div>
        </details>

        <button class="btn" style="background:#27ae60; margin:0;" onclick="reconcileSelectedUnlinked()">Rewrite All Selected Headers &amp; Link to Study</button>
    </div>

    <div id="unlk-preview-wrap" style="display:none; margin-top:12px; height:420px; border-radius:4px; overflow:hidden; box-shadow:0 1px 4px rgba(0,0,0,0.2);"></div>`;

    filterUnlinkedOrders();
}

function onUnlinkedRefMdChanged() {
    const uid = document.getElementById('unlk-ref-md')?.value;
    const doc = g_unlkRefDocs.find(d => String(d.user_id) === String(uid));
    if (!doc) return;
    const chEl = document.getElementById('unlk-dest-ch');
    const epEl = document.getElementById('unlk-dest-ep');
    if (chEl) chEl.value = doc.preferred_comm_channel || '';
    if (epEl) epEl.value = (doc.preferred_comm_channel === 'EMAIL' ? doc.email : doc.fax_number) || doc.email || '';
}

function onUnlinkedOrderSelected() {
    const oid = document.getElementById('unlk-existing-ord')?.value;
    const ord = g_unlkOrders.find(o => String(o.order_id) === String(oid));
    if (!ord) return;
    const refSel = document.getElementById('unlk-ref-md');
    if (refSel && ord.referring_md_id) {
        refSel.value = String(ord.referring_md_id);
        onUnlinkedRefMdChanged();
    }
}

/* Lightweight client-side peek of common DICOM tags (optional display only; server renames file) */
function peekDicomTags(buf) {
    const meta = { acc: '', pid: '', pname: '', mod: '', suid: '' };
    if (!buf || buf.byteLength < 140) return meta;
    const u8 = new Uint8Array(buf), dv = new DataView(buf);
    let off = (u8[128] === 68 && u8[129] === 73 && u8[130] === 67 && u8[131] === 77) ? 132 : 0;
    let limit = Math.min(buf.byteLength, 32768);
    while (off + 8 <= limit) {
        const g = dv.getUint16(off, true), e = dv.getUint16(off + 2, true);
        if (g === 0x7FE0 && e === 0x0010) break;
        off += 4;
        const c1 = u8[off], c2 = u8[off + 1];
        let len = 0;
        if (c1 >= 65 && c1 <= 90 && c2 >= 65 && c2 <= 90) {
            const vr = String.fromCharCode(c1, c2);
            off += 2;
            if (['OB','OW','OF','SQ','UT','UN','UR','UC','OD','OL'].includes(vr)) { off += 2; len = dv.getUint32(off, true); off += 4; }
            else { len = dv.getUint16(off, true); off += 2; }
        } else {
            len = dv.getUint32(off, true); off += 4;
        }
        if (len === 0xFFFFFFFF) continue;
        if (len > 0 && len < 128 && off + len <= buf.byteLength) {
            let v = '';
            for (let k = 0; k < len; k++) if (u8[off + k] > 0) v += String.fromCharCode(u8[off + k]);
            v = v.trim();
            if (g === 0x0008 && e === 0x0050) meta.acc = v;
            else if (g === 0x0010 && e === 0x0010) meta.pname = v;
            else if (g === 0x0010 && e === 0x0020) meta.pid = v;
            else if (g === 0x0008 && e === 0x0060) meta.mod = v;
            else if (g === 0x0020 && e === 0x000D) meta.suid = v;
        }
        off += len;
    }
    return meta;
}

function previewUnlinkedSop(sopUid) {
    const wrap = document.getElementById('unlk-preview-wrap');
    if (!wrap || !window.DicomViewer) return;
    wrap.style.display = 'block';
    DicomViewer.mount('unlk-preview-wrap', '', { sop: sopUid });
}

async function reconcileSelectedUnlinked() {
    const checked = Array.from(document.querySelectorAll('.unlk-chk:checked')).map(c => c.value);
    if (!checked.length) return setTechBanner('unlk-status-bar', 'Select at least one image to link and rewrite.', true);

    const mode = document.querySelector('input[name="unlk-mode"]:checked')?.value || 'EXISTING';
    const refMdId = document.getElementById('unlk-ref-md')?.value || '';
    const destCh = document.getElementById('unlk-dest-ch')?.value || '';
    const destEp = (document.getElementById('unlk-dest-ep')?.value || '').trim();
    let orderId = null;

    if (mode === 'EXISTING') {
        orderId = document.getElementById('unlk-existing-ord')?.value;
        if (!orderId) return setTechBanner('unlk-status-bar', 'Select an existing RIS order first.', true);
    } else {
        let patId = document.getElementById('unlk-pat-sel')?.value;
        if (patId === 'NEW') {
            const ln = (document.getElementById('unlk-np-last')?.value || '').trim();
            const fn = (document.getElementById('unlk-np-first')?.value || '').trim();
            const dob = (document.getElementById('unlk-np-dob')?.value || '').trim();
            const sex = document.getElementById('unlk-np-sex')?.value || 'O';
            let mrn = (document.getElementById('unlk-np-mrn')?.value || '').trim();
            if (!ln || !fn || !dob) return setTechBanner('unlk-status-bar', 'First Name, Last Name, and DOB are required for a new patient.', true);
            if (!mrn) mrn = 'MRN-' + String(Date.now()).slice(-6);

            const pRes = await execSQL(`
                INSERT INTO patients (mrn, first_name, last_name, dob, biological_sex)
                VALUES (${sqlStr(mrn)}, ${sqlStr(fn)}, ${sqlStr(ln)}, ${sqlStr(dob)}, ${sqlStr(sex)})
            `);
            if (pRes && pRes.error) return setTechBanner('unlk-status-bar', `Patient Error: ${pRes.error}`, true);
            const pidRow = await execSQL(`SELECT patient_id FROM patients WHERE mrn = ${sqlStr(mrn)} LIMIT 1`);
            patId = pidRow?.[0]?.patient_id;
        }

        const examCode = document.getElementById('unlk-exam-sel')?.value;
        const effRefId = refMdId || (g_unlkRefDocs[0]?.user_id || '');
        const prio = document.getElementById('unlk-ord-prio')?.value || 'ROUTINE';
        const acc = (document.getElementById('unlk-ord-acc')?.value || '').trim();
        const ind = (document.getElementById('unlk-ord-ind')?.value || '').trim() || 'Manual DICOM import';
        if (!patId || !examCode || !effRefId || !acc) {
            return setTechBanner('unlk-status-bar', 'Patient, Exam, Referring MD, and Accession # are required.', true);
        }

        const facRow = await execSQL(`SELECT facility_id FROM facilities ORDER BY facility_id ASC LIMIT 1`);
        const facId = facRow?.[0]?.facility_id || 1;

        const oRes = await execSQL(`
            INSERT INTO imaging_orders (accession_number, patient_id, facility_id, exam_code, referring_md_id, priority, clinical_indication, workflow_status)
            VALUES (${sqlStr(acc)}, ${patId}, ${facId}, ${sqlStr(examCode)}, ${effRefId}, '${prio}', ${sqlStr(ind)}, 'ORDERED')
        `);
        if (oRes && oRes.error) return setTechBanner('unlk-status-bar', `Order Error: ${oRes.error}`, true);
        const oidRow = await execSQL(`SELECT order_id FROM imaging_orders WHERE accession_number = ${sqlStr(acc)} LIMIT 1`);
        orderId = oidRow?.[0]?.order_id;
    }

    if (!orderId) return setTechBanner('unlk-status-bar', 'Could not resolve target order.', true);

    /* Collect custom tag overrides (format: 0xGGGG,0xEEEE,VR=VALUE separated by backtick) */
    const customRules = [];
    document.querySelectorAll('#unlk-tag-rows .unlk-tag-row').forEach(r => {
        const tag = (r.querySelector('.ut-tag')?.value || '').trim();
        const vr  = (r.querySelector('.ut-vr')?.value  || '').trim().toUpperCase();
        const val = (r.querySelector('.ut-val')?.value || '').replace(/`/g, ' ');
        if (tag && val !== '') {
            customRules.push(`${tag}${vr.length === 2 ? ',' + vr : ''}=${val}`);
        }
    });

    const qs = new URLSearchParams({
        order_id: String(orderId),
        queue_ids: checked.join(','),
        ref_md_id: refMdId,
        dest_channel: destCh,
        dest_endpoint: destEp
    }).toString();

    setTechBanner('unlk-status-bar', `Rewriting DICOM headers (${checked.length} image(s))...`);
    const res = await fetch('/api/relink_dicom?' + qs, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: customRules.join('`')
    });
    const data = await res.json();
    if (data.error) return setTechBanner('unlk-status-bar', data.error, true);

    await loadUnlinkedDicomQueue();
    setTechBanner('unlk-status-bar', `Rewrote headers and linked ${data.count} image(s) to Order #${orderId} (now READY_FOR_READ).`);
}
async function relinkUnlinkedDicom(queueId) {
    const orderId = document.getElementById(`unlk-ord-${queueId}`)?.value;
    if (!orderId) return setTechBanner('unlk-status-bar', 'Select a target RIS order first.', true);

    const res = await fetch(`/api/relink_dicom?queue_id=${encodeURIComponent(queueId)}&order_id=${encodeURIComponent(orderId)}`, { method: 'POST' });
    const data = await res.json();
    if (data.error) return setTechBanner('unlk-status-bar', data.error, true);

    await loadUnlinkedDicomQueue();
    setTechBanner('unlk-status-bar', `Image #${queueId} linked to Order #${orderId} and forwarded to READY_FOR_READ.`);
}
function setTechBanner(elId, msg, isError = false) {
    const el = document.getElementById(elId);
    if (!el) return;
    if (!msg) { el.style.display = 'none'; return; }
    el.style.display = 'block';
    el.style.background = isError ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isError ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isError ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

function scheduleTechFilter() {
    clearTimeout(g_techFilterTimer);
    g_techFilterTimer = setTimeout(refreshTechQueue, 150);
}

function clearTechFilters() {
    const setVal = (id, v) => { const el = document.getElementById(id); if (el) el.value = v; };
    setVal('tech-flt-search', '');
    setVal('tech-flt-stage', 'ACTIVE');
    setVal('tech-flt-mod', 'ALL');
    setVal('tech-flt-safety', 'ALL');
    refreshTechQueue();
}

function setTechQuickStage(stage) {
    const el = document.getElementById('tech-flt-stage');
    if (el) el.value = stage;
    refreshTechQueue();
}

async function loadTechConsole() {
    const staff = await execSQL(`
        SELECT u.user_id, u.first_name, u.last_name, u.credentials, r.role_code
        FROM staff_users u
        JOIN roles r ON u.role_id = r.role_id
        WHERE u.is_active = 1 AND r.role_code IN ('TECHNOLOGIST', 'NURSE', 'RECEPTIONIST', 'RADIOLOGIST', 'RESIDENT_FELLOW', 'SYSTEM_ADMIN')
        ORDER BY CASE r.role_code WHEN 'TECHNOLOGIST' THEN 1 WHEN 'NURSE' THEN 2 WHEN 'RECEPTIONIST' THEN 3 ELSE 4 END, u.last_name
    `);

    const staffList = Array.isArray(staff) ? staff : [];
    if (!g_techActiveUserId && staffList.length > 0) {
        g_techActiveUserId = staffList[0].user_id;
    }

    const staffOpts = staffList.map(u =>
        `<option value="${u.user_id}" ${String(u.user_id) === String(g_techActiveUserId) ? 'selected' : ''}>${u.first_name} ${u.last_name}${u.credentials ? ', ' + u.credentials : ''} [${u.role_code}]</option>`
    ).join('');

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Tech, Nurse &amp; Waiting Room Console</h2>
        <div style="display:flex; gap:8px; align-items:center; flex-wrap:wrap;">
            <label style="font-size:12px; font-weight:bold;">Active Operator:</label>
            <select id="tech-active-user" style="width:auto; min-width:230px;" onchange="g_techActiveUserId = this.value ? parseInt(this.value, 10) : null;">
                ${staffOpts || '<option value="">No staff configured</option>'}
            </select>
            <button class="btn" style="background:#e67e22; margin:0;" onclick="loadUnlinkedDicomQueue()">Unlinked DICOMs</button>
            <button class="btn" style="background:#3498db; margin:0;" onclick="refreshTechQueue()">Refresh</button>
        </div>
    </div>

    <div id="tech-status-bar" style="display:none; margin-top:10px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div id="patient-contact-banner"></div>
    <div id="tech-kpi-bar" style="display:grid; grid-template-columns:repeat(auto-fit, minmax(140px, 1fr)); gap:10px; margin:12px 0;"></div>

    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px;">
        <div style="flex:2; min-width:190px;">
            <label style="font-size:12px; font-weight:bold;">Search Patient / Ticket / Accession</label>
            <input type="text" id="tech-flt-search" placeholder="Ticket #, patient, MRN, accession..." oninput="scheduleTechFilter()">
        </div>
        <div style="min-width:165px;">
            <label style="font-size:12px; font-weight:bold;">Workflow Stage</label>
            <select id="tech-flt-stage" onchange="refreshTechQueue()">
                <option value="ACTIVE" selected>All Active Dept &amp; Scheduled</option>
                <option value="SCHEDULED">Scheduled (Awaiting Check-In)</option>
                <option value="WAITING_PREP">Waiting &amp; Prep Screening</option>
                <option value="READY_SCAN">Ready / Scanning / QA</option>
                <option value="IMAGES_CAPTURED_QA">Awaiting Tech QA Sign-off</option>
            </select>
        </div>
        <div style="min-width:120px;">
            <label style="font-size:12px; font-weight:bold;">Modality</label>
            <select id="tech-flt-mod" onchange="refreshTechQueue()">
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
        <div style="min-width:145px;">
            <label style="font-size:12px; font-weight:bold;">Safety Clearance</label>
            <select id="tech-flt-safety" onchange="refreshTechQueue()">
                <option value="ALL">All Safety States</option>
                <option value="UNCLEARED">Not Cleared / Pending</option>
                <option value="CLEARED">Cleared</option>
                <option value="HELD_NEEDS_RAD_APPROVAL">Held (Needs Rad)</option>
            </select>
        </div>
        <div>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="clearTechFilters()">Clear</button>
        </div>
    </div>

    <div id="tech-table-wrap">Loading...</div>`;

    refreshTechQueue();
}

async function refreshTechQueue() {
    const wrap = document.getElementById('tech-table-wrap');
    if (!wrap) return;

    const search = (document.getElementById('tech-flt-search')?.value || '').trim().replace(/'/g, "''");
    const stage = document.getElementById('tech-flt-stage')?.value || 'ACTIVE';
    const mod = document.getElementById('tech-flt-mod')?.value || 'ALL';
    const safety = document.getElementById('tech-flt-safety')?.value || 'ALL';

    const where = [];
    if (stage === 'ACTIVE') {
        where.push(`o.workflow_status IN ('ORDERED', 'SCHEDULED', 'IN_TRANSIT', 'ARRIVED_WAITING', 'IN_PREP_SCREENING', 'READY_FOR_SCAN', 'IN_PROGRESS', 'IMAGES_CAPTURED_QA')`);
    } else if (stage === 'SCHEDULED') {
        where.push(`o.workflow_status IN ('ORDERED', 'SCHEDULED', 'IN_TRANSIT')`);
    } else if (stage === 'WAITING_PREP') {
        where.push(`o.workflow_status IN ('ARRIVED_WAITING', 'IN_PREP_SCREENING')`);
    } else if (stage === 'READY_SCAN') {
        where.push(`o.workflow_status IN ('READY_FOR_SCAN', 'IN_PROGRESS', 'IMAGES_CAPTURED_QA')`);
    } else {
        where.push(`o.workflow_status = '${stage}'`);
    }

    if (mod !== 'ALL') where.push(`ec.modality_type = '${mod}'`);
    if (safety === 'UNCLEARED') {
        where.push(`COALESCE(s.screening_clearance_status, 'NOT_STARTED') != 'CLEARED'`);
    } else if (safety !== 'ALL') {
        where.push(`s.screening_clearance_status = '${safety}'`);
    }

    if (search) {
        where.push(`(
            COALESCE(o.queue_ticket_number, '') LIKE '%${search}%' OR
            p.first_name || ' ' || p.last_name LIKE '%${search}%' OR
            p.last_name || ', ' || p.first_name LIKE '%${search}%' OR
            p.mrn LIKE '%${search}%' OR
            o.accession_number LIKE '%${search}%'
        )`);
    }

    const whereClause = `WHERE ${where.join(' AND ')}`;

    const [rows, kpis] = await Promise.all([
        execSQL(`
            SELECT o.order_id,
                   o.accession_number,
                   o.queue_ticket_number,
                   o.priority,
                   o.patient_class,
                   o.transport_mode,
                   o.workflow_status,
                   o.scheduled_at,
                   o.arrived_at,
                   p.patient_id,
                   p.mrn,
                   p.last_name || ', ' || p.first_name AS patient_name,
                   p.contrast_allergy_flag,
                   p.mri_implant_flag,
                   p.isolation_precautions,
                   ec.modality_type,
                   ec.exam_name,
                   ec.requires_contrast,
                   ec.requires_renal_lab_check,
                   ec.requires_signed_consent,
                   COALESCE(m.room_name, 'Unassigned') AS room_name,
                   COALESCE(sp.protocol_code, '') AS protocol_code,
                   COALESCE(sp.protocol_name, '') AS protocol_name,
                   COALESCE(s.screening_clearance_status, 'NOT_STARTED') AS safety_status,
                   COALESCE(s.verified_two_patient_identifiers, 0) AS two_ids_ok,
                   (SELECT COUNT(*) FROM patient_consents pc WHERE pc.order_id = o.order_id) AS consent_count,
                   CAST((julianday('now') - julianday(COALESCE(o.arrived_at, o.scheduled_at, o.ordered_at))) * 1440 AS INTEGER) AS wait_mins
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            LEFT JOIN modalities_and_rooms m ON o.modality_id = m.modality_id
            LEFT JOIN scanner_protocols sp ON o.assigned_protocol_id = sp.protocol_id
            LEFT JOIN exam_safety_screenings s ON o.order_id = s.order_id
            ${whereClause}
            ORDER BY
                CASE o.priority
                    WHEN 'CODE_STROKE_TRAUMA' THEN 1
                    WHEN 'STAT' THEN 2
                    WHEN 'URGENT' THEN 3
                    ELSE 4
                END,
                COALESCE(o.arrived_at, o.scheduled_at, o.ordered_at) ASC
            LIMIT 150
        `),
        execSQL(`
            SELECT
                SUM(CASE WHEN workflow_status IN ('ORDERED','SCHEDULED','IN_TRANSIT') THEN 1 ELSE 0 END) AS sched_cnt,
                SUM(CASE WHEN workflow_status IN ('ARRIVED_WAITING','IN_PREP_SCREENING') THEN 1 ELSE 0 END) AS wait_cnt,
                SUM(CASE WHEN workflow_status IN ('READY_FOR_SCAN','IN_PROGRESS') THEN 1 ELSE 0 END) AS scan_cnt,
                SUM(CASE WHEN workflow_status = 'IMAGES_CAPTURED_QA' THEN 1 ELSE 0 END) AS qa_cnt
            FROM imaging_orders
        `)
    ]);

    const kpiBar = document.getElementById('tech-kpi-bar');
    if (kpiBar && Array.isArray(kpis) && kpis.length) {
        const k = kpis[0];
        const card = (lbl, val, col, stg) => `
            <div onclick="setTechQuickStage('${stg}')" style="cursor:pointer; background:#fff; border-left:4px solid ${col}; border-radius:4px; padding:10px 12px; box-shadow:0 1px 3px rgba(0,0,0,0.08);">
                <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">${lbl}</div>
                <div style="font-size:20px; font-weight:bold; color:${col}; margin-top:2px;">${val || 0}</div>
            </div>`;
        kpiBar.innerHTML =
            card('Scheduled / Due In', k.sched_cnt, '#2980b9', 'SCHEDULED') +
            card('Waiting / Prep', k.wait_cnt, '#8e44ad', 'WAITING_PREP') +
            card('Ready / In Scanner', k.scan_cnt, '#e67e22', 'READY_SCAN') +
            card('Pending Tech QA', k.qa_cnt, '#27ae60', 'IMAGES_CAPTURED_QA');
    }

    if (rows && rows.error) {
        wrap.innerHTML = `<p style="color:#e74c3c; font-weight:bold;">Database Error: ${rows.error}</p>`;
        return;
    }
    if (!Array.isArray(rows) || rows.length === 0) {
        wrap.innerHTML = '<p style="background:#fff; padding:16px; border-radius:4px;">No orders match the current console filters.</p>';
        return;
    }

    const cellStyle = 'padding:6px 5px; font-size:12px; word-break:break-word; overflow-wrap:anywhere; border-bottom:1px solid #ecf0f1;';
    const thStyle = `${cellStyle} background:#2c3e50; color:#fff; text-align:left;`;
    const btnStyle = 'padding:3px 6px; font-size:11px; margin:1px 2px 1px 0;';

    const safetyColors = {
        CLEARED: '#27ae60',
        PENDING: '#f39c12',
        NOT_STARTED: '#95a5a6',
        HELD_NEEDS_RAD_APPROVAL: '#e67e22',
        CONTRAINDICATED_CANCELLED: '#c0392b'
    };

    let html = `
    <table style="width:100%; table-layout:fixed; border-collapse:collapse; background:#fff;">
        <thead><tr>
            <th style="width:8%; ${thStyle}">TICKET</th>
            <th style="width:12%; ${thStyle}">ACCESSION</th>
            <th style="width:14%; ${thStyle}">PATIENT &amp; FLAGS</th>
            <th style="width:16%; ${thStyle}">EXAM / ROOM</th>
            <th style="width:12%; ${thStyle}">PROTOCOL</th>
            <th style="width:12%; ${thStyle}">SAFETY GATE</th>
            <th style="width:10%; ${thStyle}">STATUS</th>
            <th style="width:16%; ${thStyle}">WORKFLOW ACTIONS</th>
        </tr></thead>
        <tbody>`;

    rows.forEach(r => {
        const sCol = safetyColors[r.safety_status] || '#7f8c8d';
        const step = TECH_STATUS_FLOW[r.workflow_status] || (r.workflow_status === 'ORDERED' ? { next: 'ARRIVED_WAITING', label: 'Walk-In Check-In', bg: '#2980b9' } : null);

        const flags = [];
        if (r.contrast_allergy_flag) flags.push('<span style="background:#c0392b;color:#fff;padding:1px 4px;border-radius:2px;font-size:10px;">ALLERGY</span>');
        if (r.mri_implant_flag) flags.push('<span style="background:#8e44ad;color:#fff;padding:1px 4px;border-radius:2px;font-size:10px;">IMPLANT</span>');
        if (r.isolation_precautions) flags.push(`<span style="background:#d35400;color:#fff;padding:1px 4px;border-radius:2px;font-size:10px;">ISO:${escAttr(r.isolation_precautions)}</span>`);

        html += `
        <tr style="cursor:pointer;" onclick="renderPatientContactBanner(${r.patient_id})">
            <td style="${cellStyle}">
                <b style="font-size:13px; color:#2c3e50;">${escAttr(r.queue_ticket_number || '--')}</b>
                <div style="font-size:10px; color:#7f8c8d;">${r.priority} &bull; ${r.wait_mins ?? 0}m</div>
            </td>
            <td style="${cellStyle}">
                <b>${escAttr(r.accession_number)}</b>
                <div style="font-size:10px; color:#555;">${escAttr(r.scheduled_at || 'Unscheduled')}</div>
            </td>
            <td style="${cellStyle}">
                <b>${escAttr(r.patient_name)}</b>
                <div style="font-size:10px; color:#7f8c8d;">${escAttr(r.mrn)} &bull; ${r.transport_mode}</div>
                <div style="margin-top:2px;">${flags.join(' ')}</div>
            </td>
            <td style="${cellStyle}">
                <span style="background:#34495e; color:#fff; padding:1px 4px; border-radius:3px; font-size:10px; font-weight:bold;">${r.modality_type}</span>
                <div>${escAttr(r.exam_name)}</div>
                <div style="font-size:10px; color:#555;">Room: ${escAttr(r.room_name)}</div>
            </td>
            <td style="${cellStyle}">
                ${r.protocol_code
                    ? `<b>${escAttr(r.protocol_code)}</b><div style="font-size:10px; color:#555;">${escAttr(r.protocol_name)}</div>`
                    : `<span style="color:#e67e22; font-weight:bold; font-size:11px;">Unprotocoled</span>`}
            </td>
            <td style="${cellStyle}">
                <span style="background:${sCol}; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px; font-weight:bold; display:inline-block;">${r.safety_status}</span>
                <div style="font-size:10px; color:#555; margin-top:2px;">2-IDs: ${r.two_ids_ok ? 'Yes' : 'No'} | Consents: ${r.consent_count}</div>
            </td>
            <td style="${cellStyle}">
                <b style="font-size:11px;">${r.workflow_status}</b>
            </td>
            <td style="${cellStyle}">
                <button class="btn" style="background:#34495e; ${btnStyle}" onclick="event.stopPropagation(); openProtocolUI(${r.order_id})">Protocol</button>
                <button class="btn" style="background:#8e44ad; ${btnStyle}" onclick="event.stopPropagation(); openSafetyScreeningUI(${r.order_id})">Safety Gate</button>
                ${step ? `<button class="btn" style="background:${step.bg}; ${btnStyle}" onclick="event.stopPropagation(); advanceTechOrderStatus(${r.order_id}, '${step.next}', '${r.modality_type}', '${escAttr(r.queue_ticket_number || '')}')">${step.label}</button>` : ''}
            </td>
        </tr>`;
    });

    wrap.innerHTML = html + '</tbody></table>';
}

/* ============================================================================
 * 1. CHECK-IN & QUEUE TICKET MANAGEMENT
 * ============================================================================ */
async function advanceTechOrderStatus(orderId, nextStatus, modalityType, existingTicket) {
    const sets = [`workflow_status = '${nextStatus}'`];
    const staffId = g_techActiveUserId || 'NULL';

    if (nextStatus === 'ARRIVED_WAITING') {
        sets.push(`arrived_at = COALESCE(arrived_at, datetime('now'))`);
        if (!existingTicket) {
            const cntRes = await execSQL(`
                SELECT COUNT(*) + 1 AS seq FROM imaging_orders
                WHERE date(arrived_at) = date('now') AND queue_ticket_number IS NOT NULL
            `);
            const seq = (Array.isArray(cntRes) && cntRes[0]?.seq) ? cntRes[0].seq : 1;
            const prefix = (modalityType || 'DX').substring(0, 3).toUpperCase();
            const ticket = `${prefix}-${String(seq).padStart(2, '0')}`;
            sets.push(`queue_ticket_number = '${ticket}'`);
        }
    } else if (nextStatus === 'IN_PREP_SCREENING') {
        sets.push(`prep_started_at = COALESCE(prep_started_at, datetime('now'))`);
        if (g_techActiveUserId) sets.push(`assisting_nurse_id = COALESCE(assisting_nurse_id, ${staffId})`);
    } else if (nextStatus === 'IN_PROGRESS') {
        // Trigger trg_enforce_pre_scan_safety_clearance blocks this if safety screening is not CLEARED
        sets.push(`scan_started_at = COALESCE(scan_started_at, datetime('now'))`);
        sets.push(`mpps_status = 'IN_PROGRESS'`);
        if (g_techActiveUserId) sets.push(`performing_tech_id = COALESCE(performing_tech_id, ${staffId})`);
    } else if (nextStatus === 'IMAGES_CAPTURED_QA') {
        sets.push(`scan_completed_at = COALESCE(scan_completed_at, datetime('now'))`);
        sets.push(`mpps_status = 'COMPLETED'`);
    } else if (nextStatus === 'READY_FOR_READ') {
        sets.push(`qa_completed_at = COALESCE(qa_completed_at, datetime('now'))`);
        if (g_techActiveUserId) {
            await execSQL(`UPDATE dicom_studies SET qa_verified_by_tech_id = ${staffId} WHERE order_id = ${orderId}`);
        }
    }

    const res = await execSQL(`UPDATE imaging_orders SET ${sets.join(', ')} WHERE order_id = ${orderId}`);
    if (res && res.error) {
        setTechBanner('tech-status-bar', res.error, true);
        if (res.error.includes('SAFETY GATE VIOLATION')) {
            openSafetyScreeningUI(orderId);
        }
        return;
    }

    // Complete linked appointment slot when scan completes
    if (nextStatus === 'IMAGES_CAPTURED_QA' || nextStatus === 'READY_FOR_READ') {
        await execSQL(`UPDATE appointment_slots SET slot_status = 'COMPLETED' WHERE order_id = ${orderId} AND slot_status = 'BOOKED'`);
    }

    setTechBanner('tech-status-bar', `Order #${orderId} moved to ${nextStatus}.`);
    refreshTechQueue();
}

/* ============================================================================
 * 2. PRE-SCAN SAFETY GATE (exam_safety_screenings + patient_consents)
 * ============================================================================ */
async function openSafetyScreeningUI(orderId) {
    const [ordRes, scrRes, conRes, radsRes] = await Promise.all([
        execSQL(`
            SELECT o.*, p.mrn, p.first_name, p.last_name, p.dob, p.biological_sex,
                   p.contrast_allergy_flag, p.contrast_allergy_details, p.mri_implant_flag, p.mri_safety_notes,
                   p.egfr_value, p.creatinine_value, p.renal_lab_date, p.pregnancy_status AS pat_preg,
                   ec.exam_name, ec.modality_type, ec.requires_contrast, ec.requires_renal_lab_check, ec.requires_signed_consent
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            WHERE o.order_id = ${orderId}
        `),
        execSQL(`SELECT * FROM exam_safety_screenings WHERE order_id = ${orderId} LIMIT 1`),
        execSQL(`
            SELECT pc.*, u.first_name || ' ' || u.last_name AS explainer_name
            FROM patient_consents pc
            LEFT JOIN staff_users u ON pc.risks_explained_by_id = u.user_id
            WHERE pc.order_id = ${orderId}
            ORDER BY pc.signed_at DESC
        `),
        execSQL(`
            SELECT u.user_id, u.first_name, u.last_name
            FROM staff_users u JOIN roles r ON u.role_id = r.role_id
            WHERE u.is_active = 1 AND r.can_override_safety = 1
            ORDER BY u.last_name
        `)
    ]);

    if (!Array.isArray(ordRes) || !ordRes.length) return;
    const o = ordRes[0];
    const s = (Array.isArray(scrRes) && scrRes.length) ? scrRes[0] : null;
    const consents = Array.isArray(conRes) ? conRes : [];
    const rads = Array.isArray(radsRes) ? radsRes : [];

    const pregVal = s?.pregnancy_status || (o.biological_sex === 'M' ? 'NOT_APPLICABLE' : 'UNSURE');
    const clearVal = s?.screening_clearance_status || 'PENDING';
    const mriStat = s?.mri_implant_safety_status || (o.modality_type === 'MRI' ? 'MR_SAFE' : 'NA');
    const chk = (val, def = 0) => ((s ? s[val] : def) ? 'checked' : '');

    const radOpts = `<option value="">-- None --</option>` + rads.map(r =>
        `<option value="${r.user_id}" ${String(s?.radiologist_override_by_id) === String(r.user_id) ? 'selected' : ''}>Dr. ${r.first_name} ${r.last_name}</option>`
    ).join('');

    const consentRows = consents.length
        ? consents.map(c => `<div style="padding:5px 8px; border-bottom:1px solid #eee; font-size:12px;">
            <b>${c.consent_type}</b> &mdash; Signed by <b>${escAttr(c.signed_by_name)}</b> (${escAttr(c.relationship_to_patient)}) | Explained by ${escAttr(c.explainer_name || 'Staff')} @ ${c.signed_at}
          </div>`).join('')
        : '<div style="padding:6px; font-size:12px; color:#7f8c8d;">No signed consents recorded for this order yet.</div>';

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Pre-Scan Safety Gate: ${escAttr(o.accession_number)}</h2>
        <button class="btn" style="background:#34495e; margin:0;" onclick="loadTechConsole()">&larr; Back to Tech Console</button>
    </div>

    <div id="safety-status-bar" style="display:none; margin-top:8px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>

    <div style="background:#fff; border-left:5px solid #8e44ad; border-radius:4px; padding:10px 14px; margin:10px 0; box-shadow:0 1px 3px rgba(0,0,0,0.08); display:flex; flex-wrap:wrap; gap:18px; font-size:13px;">
        <div><b>${escAttr(o.last_name)}, ${escAttr(o.first_name)}</b><div style="font-size:11px; color:#555;">MRN: ${escAttr(o.mrn)} | DOB: ${o.dob} | Sex: ${o.biological_sex}</div></div>
        <div><b>${escAttr(o.exam_name)} [${o.modality_type}]</b><div style="font-size:11px; color:#555;">Requires Contrast: ${o.requires_contrast ? 'YES' : 'No'} | Renal Check: ${o.requires_renal_lab_check ? 'YES' : 'No'} | Consent: ${o.requires_signed_consent ? 'YES' : 'No'}</div></div>
        ${o.contrast_allergy_flag ? `<div style="color:#c0392b; font-weight:bold;">PATIENT ALLERGY FLAG: ${escAttr(o.contrast_allergy_details || 'Yes')}</div>` : ''}
        ${o.mri_implant_flag ? `<div style="color:#8e44ad; font-weight:bold;">PATIENT IMPLANT FLAG: ${escAttr(o.mri_safety_notes || 'Yes')}</div>` : ''}
    </div>

    <div style="display:grid; grid-template-columns:minmax(380px, 1.3fr) minmax(300px, 1fr); gap:14px; align-items:start;">
        <!-- LEFT: SAFETY SCREENING CHECKLIST -->
        <div style="background:#fff; padding:16px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:13px;">
            <h3 style="margin:0 0 10px 0; color:#2c3e50; border-bottom:2px solid #8e44ad; padding-bottom:5px;">Time-Out &amp; Clinical Safety Screening</h3>

            <div style="background:#fef9e7; border:1px solid #f1c40f; padding:10px; border-radius:4px; margin-bottom:12px;">
                <label style="display:block; font-weight:bold; margin-bottom:6px; cursor:pointer;">
                    <input type="checkbox" id="sf-2id" style="width:auto;" ${chk('verified_two_patient_identifiers', 0)}>
                    Verified Two Patient Identifiers (Full Name &amp; DOB/MRN) *
                </label>
                <label style="display:block; font-weight:bold; cursor:pointer;">
                    <input type="checkbox" id="sf-site" style="width:auto;" ${chk('verified_correct_exam_and_site', 0)}>
                    Verified Correct Exam, Protocol &amp; Anatomical Laterality/Site *
                </label>
            </div>

            <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px;">
                <div>
                    <label style="font-size:12px; font-weight:bold;">Pregnancy Status</label>
                    <select id="sf-preg">
                        ${['NOT_APPLICABLE','NEGATIVE_BY_HISTORY','NEGATIVE_BY_LAB','POSITIVE','UNSURE'].map(p =>
                            `<option value="${p}" ${pregVal === p ? 'selected' : ''}>${p}</option>`
                        ).join('')}
                    </select>
                </div>
                <div>
                    <label style="font-size:12px; font-weight:bold;">Last Menstrual Period (LMP)</label>
                    <input type="date" id="sf-lmp" value="${escAttr(s?.last_menstrual_period || '')}">
                </div>
            </div>

            <div style="display:grid; grid-template-columns:1fr 1fr 1fr; gap:10px; margin-bottom:10px;">
                <div>
                    <label style="font-size:12px; font-weight:bold;">eGFR (mL/min)</label>
                    <input type="number" step="0.1" id="sf-egfr" value="${escAttr(s?.egfr_ml_min ?? o.egfr_value ?? '')}">
                </div>
                <div>
                    <label style="font-size:12px; font-weight:bold;">Creatinine (mg/dL)</label>
                    <input type="number" step="0.01" id="sf-creat" value="${escAttr(s?.serum_creatinine_mg_dl ?? o.creatinine_value ?? '')}">
                </div>
                <div>
                    <label style="font-size:12px; font-weight:bold;">Renal Lab Drawn Date</label>
                    <input type="date" id="sf-rdate" value="${escAttr(s?.renal_lab_drawn_date || o.renal_lab_date || '')}">
                </div>
            </div>

            <div style="border-top:1px solid #eee; padding-top:8px; margin-bottom:10px;">
                <b style="font-size:12px; color:#2c3e50;">Contrast &amp; Premedication</b>
                <div style="display:flex; flex-wrap:wrap; gap:14px; margin-top:6px;">
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-calg" style="width:auto;" ${chk('has_prior_contrast_reaction', o.contrast_allergy_flag)}> Prior Contrast Reaction</label>
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-premed-req" style="width:auto;" ${chk('premedication_required', 0)}> Premedication Required</label>
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-premed-ok" style="width:auto;" ${chk('premedication_verified_complete', 0)}> Premedication Verified Complete</label>
                </div>
            </div>

            <div style="border-top:1px solid #eee; padding-top:8px; margin-bottom:10px;">
                <b style="font-size:12px; color:#2c3e50;">MRI Zone III Ferromagnetic &amp; Implant Screening</b>
                <div style="display:grid; grid-template-columns:1fr 1fr; gap:6px; margin-top:6px;">
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-pace" style="width:auto;" ${chk('has_cardiac_pacemaker_icd', 0)}> Cardiac Pacemaker / ICD</label>
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-clip" style="width:auto;" ${chk('has_aneurysm_clips_coils', 0)}> Aneurysm Clips / Coils</label>
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-coch" style="width:auto;" ${chk('has_cochlear_neurostimulator', 0)}> Cochlear / Neurostimulator</label>
                    <label style="cursor:pointer;"><input type="checkbox" id="sf-orb" style="width:auto;" ${chk('has_metallic_orbital_foreign_body', 0)}> Metallic Orbital Foreign Body</label>
                </div>
                <div style="margin-top:8px;">
                    <label style="font-size:12px; font-weight:bold;">MRI Implant Safety Classification</label>
                    <select id="sf-mristat">
                        ${['NA','MR_SAFE','MR_CONDITIONAL_CLEARED','MR_UNSAFE_CONTRAINDICATED'].map(m =>
                            `<option value="${m}" ${mriStat === m ? 'selected' : ''}>${m}</option>`
                        ).join('')}
                    </select>
                </div>
            </div>

            <div style="border-top:1px solid #eee; padding-top:10px; margin-bottom:12px;">
                <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px;">
                    <div>
                        <label style="font-size:12px; font-weight:bold;">Safety Gate Clearance Status *</label>
                        <select id="sf-clearance">
                            ${['PENDING','CLEARED','HELD_NEEDS_RAD_APPROVAL','CONTRAINDICATED_CANCELLED'].map(c =>
                                `<option value="${c}" ${clearVal === c ? 'selected' : ''}>${c}</option>`
                            ).join('')}
                        </select>
                    </div>
                    <div>
                        <label style="font-size:12px; font-weight:bold;">Radiologist Override By</label>
                        <select id="sf-override-md">${radOpts}</select>
                    </div>
                </div>
                <div style="margin-top:8px;">
                    <label style="font-size:12px; font-weight:bold;">Radiologist Override / Safety Notes</label>
                    <input type="text" id="sf-override-notes" value="${escAttr(s?.radiologist_override_notes || '')}" placeholder="e.g. Cleared with reduced contrast dose or MR conditional parameters">
                </div>
            </div>

            <button class="btn" style="background:#27ae60;" onclick="submitSafetyScreening(${orderId}, ${o.patient_id}, false)">Save Safety Screening</button>
            <button class="btn" style="background:#2980b9; margin-left:6px;" onclick="submitSafetyScreening(${orderId}, ${o.patient_id}, true)">Clear &amp; Mark Ready for Scan &rarr;</button>
        </div>

        <!-- RIGHT: SIGNED PATIENT CONSENTS -->
        <div style="background:#fff; padding:16px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:13px;">
            <h3 style="margin:0 0 10px 0; color:#2c3e50; border-bottom:2px solid #2980b9; padding-bottom:5px;">Signed Patient Consents</h3>
            <div style="margin-bottom:12px;">${consentRows}</div>

            <div style="background:#f8f9fa; border:1px solid #ddd; border-radius:4px; padding:10px;">
                <b style="font-size:12px;">Record New Consent Signature</b>
                <div style="margin-top:8px;">
                    <label style="font-size:11px; font-weight:bold;">Consent Type</label>
                    <select id="con-type">
                        <option value="IV_CONTRAST">IV_CONTRAST</option>
                        <option value="MRI_SAFETY">MRI_SAFETY</option>
                        <option value="PREGNANCY_RADIATION_WAIVER">PREGNANCY_RADIATION_WAIVER</option>
                        <option value="INTERVENTIONAL_PROCEDURE">INTERVENTIONAL_PROCEDURE</option>
                        <option value="SEDATION">SEDATION</option>
                    </select>
                </div>
                <div style="display:grid; grid-template-columns:1fr 1fr; gap:8px; margin-top:8px;">
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Signed By Name *</label>
                        <input type="text" id="con-signer" value="${escAttr(o.first_name + ' ' + o.last_name)}">
                    </div>
                    <div>
                        <label style="font-size:11px; font-weight:bold;">Relationship</label>
                        <input type="text" id="con-rel" value="SELF">
                    </div>
                </div>
                <div style="margin-top:8px;">
                    <label style="font-size:11px; font-weight:bold;">Signature Reference / Pad Hash</label>
                    <input type="text" id="con-ref" value="SIG-PAD-${Date.now().toString().slice(-6)}">
                </div>
                <button class="btn" style="background:#2980b9; margin-top:10px;" onclick="submitPatientConsent(${orderId})">+ Record Consent</button>
            </div>
        </div>
    </div>`;
}

async function submitSafetyScreening(orderId, patientId, markReadyForScan = false) {
    if (!g_techActiveUserId) {
        return setTechBanner('safety-status-bar', 'Select an active operator in the Tech Console header first.', true);
    }

    const isChk = id => document.getElementById(id)?.checked ? 1 : 0;
    const val = id => (document.getElementById(id)?.value || '').trim();

    const twoIds = isChk('sf-2id');
    const siteOk = isChk('sf-site');
    let clearance = markReadyForScan ? 'CLEARED' : val('sf-clearance');

    if (clearance === 'CLEARED' && (!twoIds || !siteOk)) {
        return setTechBanner('safety-status-bar', 'Two patient identifiers and correct exam/site must both be verified before marking CLEARED.', true);
    }

    const egfr = sqlNum(val('sf-egfr'));
    const creat = sqlNum(val('sf-creat'));
    const rdate = sqlStr(val('sf-rdate'), true);
    const lmp = sqlStr(val('sf-lmp'), true);
    const preg = sqlStr(val('sf-preg'));
    const mriStat = sqlStr(val('sf-mristat'));
    const ovMd = sqlInt(val('sf-override-md'));
    const ovNotes = sqlStr(val('sf-override-notes'), true);

    const res = await execSQL(`
        INSERT INTO exam_safety_screenings (
            order_id, screened_by_id, verified_two_patient_identifiers, verified_correct_exam_and_site,
            pregnancy_status, last_menstrual_period, serum_creatinine_mg_dl, egfr_ml_min, renal_lab_drawn_date,
            has_prior_contrast_reaction, premedication_required, premedication_verified_complete,
            has_cardiac_pacemaker_icd, has_aneurysm_clips_coils, has_cochlear_neurostimulator,
            has_metallic_orbital_foreign_body, mri_implant_safety_status, screening_clearance_status,
            radiologist_override_by_id, radiologist_override_notes, screened_at
        ) VALUES (
            ${orderId}, ${g_techActiveUserId}, ${twoIds}, ${siteOk},
            ${preg}, ${lmp}, ${creat}, ${egfr}, ${rdate},
            ${isChk('sf-calg')}, ${isChk('sf-premed-req')}, ${isChk('sf-premed-ok')},
            ${isChk('sf-pace')}, ${isChk('sf-clip')}, ${isChk('sf-coch')},
            ${isChk('sf-orb')}, ${mriStat}, '${clearance}',
            ${ovMd}, ${ovNotes}, datetime('now')
        )
        ON CONFLICT(order_id) DO UPDATE SET
            screened_by_id = excluded.screened_by_id,
            verified_two_patient_identifiers = excluded.verified_two_patient_identifiers,
            verified_correct_exam_and_site = excluded.verified_correct_exam_and_site,
            pregnancy_status = excluded.pregnancy_status,
            last_menstrual_period = excluded.last_menstrual_period,
            serum_creatinine_mg_dl = excluded.serum_creatinine_mg_dl,
            egfr_ml_min = excluded.egfr_ml_min,
            renal_lab_drawn_date = excluded.renal_lab_drawn_date,
            has_prior_contrast_reaction = excluded.has_prior_contrast_reaction,
            premedication_required = excluded.premedication_required,
            premedication_verified_complete = excluded.premedication_verified_complete,
            has_cardiac_pacemaker_icd = excluded.has_cardiac_pacemaker_icd,
            has_aneurysm_clips_coils = excluded.has_aneurysm_clips_coils,
            has_cochlear_neurostimulator = excluded.has_cochlear_neurostimulator,
            has_metallic_orbital_foreign_body = excluded.has_metallic_orbital_foreign_body,
            mri_implant_safety_status = excluded.mri_implant_safety_status,
            screening_clearance_status = excluded.screening_clearance_status,
            radiologist_override_by_id = excluded.radiologist_override_by_id,
            radiologist_override_notes = excluded.radiologist_override_notes,
            screened_at = datetime('now')
    `);

    if (res && res.error) {
        return setTechBanner('safety-status-bar', res.error, true);
    }

    // Sync updated renal labs back to patient record if entered
    if (egfr !== 'NULL' || creat !== 'NULL') {
        await execSQL(`
            UPDATE patients
            SET egfr_value = COALESCE(${egfr}, egfr_value),
                creatinine_value = COALESCE(${creat}, creatinine_value),
                renal_lab_date = COALESCE(${rdate}, renal_lab_date),
                updated_at = datetime('now')
            WHERE patient_id = ${patientId}
        `);
    }

    if (clearance === 'CONTRAINDICATED_CANCELLED') {
        await execSQL(`UPDATE imaging_orders SET workflow_status = 'CANCELLED', cancellation_reason = 'Contraindicated during safety screening' WHERE order_id = ${orderId}`);
    } else if (markReadyForScan) {
        await execSQL(`
            UPDATE imaging_orders
            SET workflow_status = 'READY_FOR_SCAN',
                arrived_at = COALESCE(arrived_at, datetime('now')),
                prep_started_at = COALESCE(prep_started_at, datetime('now'))
            WHERE order_id = ${orderId}
        `);
        await loadTechConsole();
        setTechBanner('tech-status-bar', `Safety Gate CLEARED and Order #${orderId} is READY_FOR_SCAN.`);
        return;
    }

    setTechBanner('safety-status-bar', `Safety screening saved (${clearance}).`);
}

async function submitPatientConsent(orderId) {
    if (!g_techActiveUserId) {
        return setTechBanner('safety-status-bar', 'Select an active operator in the Tech Console header first.', true);
    }
    const cType = document.getElementById('con-type')?.value || 'IV_CONTRAST';
    const signer = (document.getElementById('con-signer')?.value || '').trim();
    const rel = (document.getElementById('con-rel')?.value || 'SELF').trim();
    const ref = (document.getElementById('con-ref')?.value || '').trim();
    if (!signer) return setTechBanner('safety-status-bar', 'Signer name is required.', true);

    const res = await execSQL(`
        INSERT INTO patient_consents (order_id, consent_type, risks_explained_by_id, signed_by_name, relationship_to_patient, signature_capture_ref)
        VALUES (${orderId}, '${cType}', ${g_techActiveUserId}, ${sqlStr(signer)}, ${sqlStr(rel)}, ${sqlStr(ref, true)})
    `);
    if (res && res.error) return setTechBanner('safety-status-bar', res.error, true);
    openSafetyScreeningUI(orderId);
}

/* ============================================================================
 * 3. SCANNER PROTOCOLING (scanner_protocols -> imaging_orders)
 * ============================================================================ */
let g_protoCatalog = [];

let g_unlkOrders = [];
let g_unlkRefDocs = [];

async function openProtocolUI(orderId) {
    const [ordRes, protosRes, roomsRes] = await Promise.all([
        execSQL(`
            SELECT o.*, p.mrn, p.first_name, p.last_name, p.egfr_value, p.contrast_allergy_flag,
                   ec.exam_name, ec.modality_type, ec.body_part
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            WHERE o.order_id = ${orderId}
        `),
        execSQL(`SELECT * FROM scanner_protocols ORDER BY modality_type, protocol_code`),
        execSQL(`SELECT modality_id, room_name, modality_type FROM modalities_and_rooms WHERE operational_status = 'ONLINE' ORDER BY room_name`)
    ]);

    if (!Array.isArray(ordRes) || !ordRes.length) return;
    const o = ordRes[0];
    g_protoCatalog = Array.isArray(protosRes) ? protosRes : [];
    const rooms = Array.isArray(roomsRes) ? roomsRes : [];

    const matching = g_protoCatalog.filter(p => p.modality_type === o.modality_type);
    const others = g_protoCatalog.filter(p => p.modality_type !== o.modality_type);

    const protoOpts = `<option value="">-- Select Scanner Protocol --</option>` +
        (matching.length ? `<optgroup label="${o.modality_type} Protocols">` + matching.map(p =>
            `<option value="${p.protocol_id}" ${String(o.assigned_protocol_id) === String(p.protocol_id) ? 'selected' : ''}>${escAttr(p.protocol_code)} - ${escAttr(p.protocol_name)}</option>`
        ).join('') + `</optgroup>` : '') +
        (others.length ? `<optgroup label="Other Modalities">` + others.map(p =>
            `<option value="${p.protocol_id}" ${String(o.assigned_protocol_id) === String(p.protocol_id) ? 'selected' : ''}>[${p.modality_type}] ${escAttr(p.protocol_code)} - ${escAttr(p.protocol_name)}</option>`
        ).join('') + `</optgroup>` : '');

    const roomOpts = `<option value="NULL">-- Keep / Unassigned --</option>` + rooms.map(r =>
        `<option value="${r.modality_id}" ${String(o.modality_id) === String(r.modality_id) ? 'selected' : ''}>${escAttr(r.room_name)} [${r.modality_type}]</option>`
    ).join('');

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Scanner Protocoling: ${escAttr(o.accession_number)}</h2>
        <button class="btn" style="background:#34495e; margin:0;" onclick="loadTechConsole()">&larr; Back to Tech Console</button>
    </div>

    <div id="proto-status-bar" style="display:none; margin-top:8px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>

    <div style="background:#fff; padding:18px; max-width:720px; margin-top:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:13px;">
        <div style="background:#f8f9fa; padding:10px; border-radius:4px; margin-bottom:12px;">
            <b>Patient:</b> ${escAttr(o.last_name)}, ${escAttr(o.first_name)} (${escAttr(o.mrn)}) &nbsp;|&nbsp;
            <b>eGFR:</b> ${o.egfr_value ?? 'N/A'} &nbsp;|&nbsp;
            <b>Contrast Allergy:</b> ${o.contrast_allergy_flag ? '<span style="color:#c0392b;font-weight:bold;">YES</span>' : 'No'}<br>
            <b>Exam:</b> ${escAttr(o.exam_name)} [${o.modality_type}]<br>
            <b>Clinical Indication:</b> ${escAttr(o.clinical_indication)}
        </div>

        <div style="display:grid; grid-template-columns:1.4fr 1fr; gap:10px; margin-bottom:12px;">
            <div>
                <label style="font-size:12px; font-weight:bold;">Assigned Scanner Protocol *</label>
                <select id="pr-protocol" onchange="previewSelectedProtocol(this.value)">${protoOpts}</select>
            </div>
            <div>
                <label style="font-size:12px; font-weight:bold;">Target Modality Room</label>
                <select id="pr-room">${roomOpts}</select>
            </div>
        </div>

        <div id="pr-details-box" style="background:#eef5fb; border:1px solid #aed6f1; border-radius:4px; padding:10px; margin-bottom:12px; font-size:12px;"></div>

        <label style="font-size:12px; font-weight:bold;">Custom Protocol Instructions / Radiologist Notes</label>
        <textarea id="pr-notes" rows="3" placeholder="e.g. Add delayed phase at 5 mins; reduce contrast volume to 60 mL...">${escAttr(o.protocol_custom_notes || '')}</textarea><br><br>

        <button class="btn" style="background:#27ae60;" onclick="submitOrderProtocol(${orderId})">Save Protocol Assignment</button>
    </div>`;

    previewSelectedProtocol(o.assigned_protocol_id || '');
}

function previewSelectedProtocol(protoId) {
    const box = document.getElementById('pr-details-box');
    if (!box) return;
    const p = g_protoCatalog.find(x => String(x.protocol_id) === String(protoId));
    if (!p) {
        box.innerHTML = '<i>Select a protocol to view default contrast, injection rate, scan delay, and reconstruction parameters.</i>';
        return;
    }
    box.innerHTML = `
        <b>${escAttr(p.protocol_code)} &mdash; ${escAttr(p.protocol_name)}</b><br>
        <b>Default Contrast:</b> ${escAttr(p.contrast_agent_default || 'None')} ${p.contrast_volume_ml_default ? `(${p.contrast_volume_ml_default} mL @ ${p.injection_rate_ml_sec || '-'} mL/s)` : ''}<br>
        <b>Scan Delay:</b> ${escAttr(p.scan_delay_instructions || 'Standard')}<br>
        <b>Reconstructions:</b> ${escAttr(p.reconstruction_instructions || 'Standard')}`;
}

async function submitOrderProtocol(orderId) {
    const protoId = document.getElementById('pr-protocol')?.value;
    const roomId = document.getElementById('pr-room')?.value || 'NULL';
    const notes = (document.getElementById('pr-notes')?.value || '').trim();

    if (!protoId) {
        return setTechBanner('proto-status-bar', 'Select a scanner protocol first.', true);
    }

    const res = await execSQL(`
        UPDATE imaging_orders
        SET assigned_protocol_id = ${protoId},
            modality_id = COALESCE(${roomId}, modality_id),
            protocol_custom_notes = ${sqlStr(notes, true)}
        WHERE order_id = ${orderId}
    `);
    if (res && res.error) return setTechBanner('proto-status-bar', res.error, true);

    await loadTechConsole();
    setTechBanner('tech-status-bar', `Scanner protocol assigned to Order #${orderId} (DICOM MWL updated).`);
}function toggleUnlinkedAssignMode() {
    const mode = document.querySelector('input[name="unlk-mode"]:checked')?.value || 'EXISTING';
    document.getElementById('unlk-mode-existing').style.display = mode === 'EXISTING' ? 'block' : 'none';
    document.getElementById('unlk-mode-new').style.display = mode === 'NEW_ORDER' ? 'block' : 'none';
}

async function uploadUnlinkedFiles(fileList) {
    if (!fileList || !fileList.length) return;
    const skipExt = /\.(txt|xml|ini|json|csv|html|htm|pdf|db|exe|dll|bat|js|css|md|log)$/i;
    const files = Array.from(fileList).filter(f => {
        const nm = (f.name || '').toLowerCase();
        if (!nm || nm === 'dicomdir' || nm === 'thumbs.db' || nm === '.ds_store' || nm === 'desktop.ini') return false;
        if (skipExt.test(nm)) return false;
        return f.size >= 132;
    });

    if (!files.length) {
        return setTechBanner('unlk-status-bar', 'No valid image files found in selection.', true);
    }

    let okCnt = 0, failCnt = 0;
    for (let i = 0; i < files.length; i++) {
        setTechBanner('unlk-status-bar', `Uploading & renaming image ${i + 1} of ${files.length}...`);
        try {
            const buf = await files[i].arrayBuffer();
            const m = peekDicomTags(buf);
            const qs = new URLSearchParams({
                acc: m.acc, pid: m.pid, pname: m.pname, mod: m.mod, suid: m.suid
            }).toString();
            const res = await fetch('/api/upload_unlinked?' + qs, {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: buf
            });
            const data = await res.json();
            if (data.error) failCnt++; else okCnt++;
        } catch (e) {
            failCnt++;
        }
    }

    await loadUnlinkedDicomQueue();
    setTechBanner('unlk-status-bar', `Uploaded ${okCnt} image(s) into staging queue${failCnt ? ` (${failCnt} failed)` : ''}.`, failCnt > 0 && okCnt === 0);
}

