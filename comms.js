/* ============================================================================
 * RIS Communication & Dispatch Queue Monitor (correspondence_dispatch_queue)
 * ============================================================================ */

let g_commFilterTimer = null;
let g_commAutoRefreshTimer = null;
let g_commSortCol = 'q.queued_at';
let g_commSortAsc = false;
let g_commExpandedId = null;
let g_commLastRows = {};

const COMM_STATUS_COLORS = {
    QUEUED: '#f39c12',
    TRANSMITTING: '#2980b9',
    SENT: '#27ae60',
    DELIVERED_ACK: '#16a085',
    ACKNOWLEDGED: '#1e8449',
    FAILED: '#e74c3c',
    FAILED_RETRYING: '#d35400',
    PERMANENT_FAILURE: '#922b21'
};

const COMM_CHANNEL_COLORS = {
    EMAIL: '#3498db',
    SMS: '#9b59b6',
    PHONE_TTS: '#1abc9c',
    PHONE_VERBAL: '#8e44ad',
    HL7_ORU_R01: '#2c3e50',
    HL7_ORM_O01: '#34495e',
    HL7_FHIR_PUSH: '#1f618d',
    FAX: '#7f8c8d',
    PATIENT_PORTAL: '#2e86c1'
};

const AUTO_DISPATCH_CHANNELS = new Set([
    'EMAIL', 'SMS', 'PHONE_TTS', 'HL7_ORU_R01', 'HL7_ORM_O01', 'HL7_FHIR_PUSH', 'FAX', 'PATIENT_PORTAL'
]);

async function dispatchCommRow(dispatchId, filterStatus = 'ALL') {
    const bar = document.getElementById('comm-status-bar');
    const setBar = (msg, isErr) => {
        if (!bar) return;
        bar.style.display = 'block';
        bar.style.background = isErr ? '#fdedec' : '#eafaf1';
        bar.style.border = `1px solid ${isErr ? '#e74c3c' : '#2ecc71'}`;
        bar.style.color = isErr ? '#922b21' : '#1e8449';
        bar.innerText = msg;
    };

    setBar(`Dispatching #${dispatchId}...`, false);
    try {
        const res = await fetch('/api/dispatch_reminder', {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: String(dispatchId)
        });
        const data = await res.json();
        await loadCommsQueue(filterStatus);
        if (data.error) setBar(`Dispatch #${dispatchId} failed: ${data.error}`, true);
        else setBar(`Dispatch #${dispatchId} status: ${data.status}`, false);
    } catch (e) {
        setBar(`Dispatch error: ${e.message}`, true);
    }
}
async function loadCommsQueue(filterStatus = 'ALL') {
    const whereClause = filterStatus && filterStatus !== 'ALL'
        ? `WHERE q.delivery_status = '${filterStatus.replace(/'/g, "''")}'`
        : '';

    const rows = await execSQL(`
        SELECT q.dispatch_id,
               q.report_id,
               q.order_id,
               q.patient_id,
               q.channel,
               COALESCE(q.destination_endpoint, '') AS destination_endpoint,
               COALESCE(q.payload_content, '') AS payload_content,
               q.delivery_status,
               COALESCE(q.retry_count, 0) AS retry_count,
               COALESCE(q.error_message, '') AS error_message,
               q.queued_at,
               q.sent_at,
               COALESCE(p.last_name || ', ' || p.first_name, '') AS patient_name,
               COALESCE(p.mrn, '') AS mrn,
               COALESCE(o.accession_number, '') AS accession_number,
               COALESCE(u.first_name || ' ' || u.last_name, '') AS recipient_name
        FROM correspondence_dispatch_queue q
        LEFT JOIN patients p ON q.patient_id = p.patient_id
        LEFT JOIN imaging_orders o ON q.order_id = o.order_id
        LEFT JOIN staff_users u ON q.recipient_user_id = u.user_id
        ${whereClause}
        ORDER BY
            CASE q.delivery_status
                WHEN 'FAILED' THEN 1
                WHEN 'FAILED_RETRYING' THEN 2
                WHEN 'QUEUED' THEN 3
                WHEN 'PERMANENT_FAILURE' THEN 4
                ELSE 5
            END,
            q.queued_at DESC
        LIMIT 200
    `);

    const statuses = ['ALL', 'QUEUED', 'FAILED', 'FAILED_RETRYING', 'PERMANENT_FAILURE', 'TRANSMITTING', 'SENT'];
    const filterOpts = statuses.map(s =>
        `<option value="${s}" ${s === filterStatus ? 'selected' : ''}>${s}</option>`
    ).join('');

    const cellStyle = 'padding:6px 5px; font-size:12px; border-bottom:1px solid #ecf0f1; word-break:break-word; vertical-align:top;';
    const thStyle = `${cellStyle} background:#2c3e50; color:#fff; text-align:left;`;

    let bodyHtml = '';
    if (rows && rows.error) {
        bodyHtml = `<tr><td colspan="7" style="padding:14px; color:#c0392b; font-weight:bold;">SQL Error: ${escAttr(rows.error)}</td></tr>`;
    } else if (!Array.isArray(rows) || rows.length === 0) {
        bodyHtml = `<tr><td colspan="7" style="padding:14px;">No items in the communications dispatch queue.</td></tr>`;
    } else {
        const statusColors = {
            SENT: '#27ae60',
            QUEUED: '#2980b9',
            TRANSMITTING: '#16a085',
            FAILED: '#e74c3c',
            FAILED_RETRYING: '#e67e22',
            PERMANENT_FAILURE: '#922b21'
        };

        bodyHtml = rows.map(r => {
            const stCol = statusColors[r.delivery_status] || '#7f8c8d';
            const canAutoSend = AUTO_DISPATCH_CHANNELS.has(r.channel) && r.delivery_status !== 'SENT';
            const recipientLabel = r.recipient_name
                ? `Dr. ${escAttr(r.recipient_name)}`
                : (r.patient_name ? `${escAttr(r.patient_name)} (${escAttr(r.mrn)})` : 'Unspecified');

            return `
            <tr>
                <td style="${cellStyle}">
                    <b>#${r.dispatch_id}</b>
                    <div style="font-size:10px; color:#7f8c8d;">${escAttr(r.queued_at || '')}</div>
                </td>
                <td style="${cellStyle}">
                    <b>${recipientLabel}</b>
                    ${r.patient_name && r.recipient_name ? `<div style="font-size:10px; color:#555;">Pt: ${escAttr(r.patient_name)}</div>` : ''}
                </td>
                <td style="${cellStyle}">
                    <b>${escAttr(r.accession_number || '-')}</b>
                    ${r.report_id ? `<div style="font-size:10px; color:#8e44ad; font-weight:bold;">Report #${r.report_id}</div>` : ''}
                </td>
                <td style="${cellStyle}">
                    <span style="background:#34495e; color:#fff; padding:1px 5px; border-radius:3px; font-size:10px; font-weight:bold;">${escAttr(r.channel || 'NONE')}</span>
                    <div style="font-size:11px; margin-top:2px;">${escAttr(r.destination_endpoint || '(missing endpoint)')}</div>
                </td>
                <td style="${cellStyle}">
                    <span style="background:${stCol}; color:#fff; padding:2px 6px; border-radius:3px; font-size:10px; font-weight:bold;">${escAttr(r.delivery_status)}</span>
                    <div style="font-size:10px; color:#7f8c8d; margin-top:2px;">Retries: ${r.retry_count}${r.sent_at ? ' | ' + escAttr(r.sent_at) : ''}</div>
                </td>
                <td style="${cellStyle}">
                    <div style="max-height:48px; overflow:hidden; font-size:11px; color:#333;" title="${escAttr(r.payload_content)}">${escAttr(r.payload_content).slice(0, 140)}</div>
                    ${r.error_message ? `<div style="font-size:11px; color:#c0392b; font-weight:bold; margin-top:3px;">${escAttr(r.error_message)}</div>` : ''}
                </td>
                <td style="${cellStyle} white-space:nowrap;">
                    ${canAutoSend ? `<button class="btn" style="background:#27ae60; padding:3px 7px; font-size:11px; margin:1px;" onclick="dispatchCommRow(${r.dispatch_id}, '${filterStatus}')">Send Now</button>` : ''}
                    ${r.report_id ? `<button class="btn" style="background:#8e44ad; padding:3px 7px; font-size:11px; margin:1px;" onclick="previewCommPaperReport(${r.report_id})">Preview Report</button>` : ''}
                    <button class="btn" style="background:#e67e22; padding:3px 7px; font-size:11px; margin:1px;" onclick="openFixCommChannel(${r.dispatch_id}, '${escAttr(r.channel || '')}', '${escAttr(r.destination_endpoint || '')}')">Fix Channel</button>
                </td>
            </tr>`;
        }).join('');
    }

    document.getElementById('main-content').innerHTML = `
    <div class="header-flex">
        <h2>Communications &amp; Report Dispatch Queue</h2>
        <div style="display:flex; gap:8px; align-items:center; flex-wrap:wrap;">
            <label style="font-size:12px; font-weight:bold;">Status:</label>
            <select style="width:auto;" onchange="loadCommsQueue(this.value)">${filterOpts}</select>
            <button class="btn" style="background:#3498db; margin:0;" onclick="loadCommsQueue('${filterStatus}')">Refresh</button>
        </div>
    </div>
    <div id="comm-status-bar" style="display:none; margin:10px 0; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div style="background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-top:10px; overflow-x:auto;">
        <table style="width:100%; border-collapse:collapse;">
            <thead><tr>
                <th style="${thStyle}">ID / Queued</th>
                <th style="${thStyle}">Recipient</th>
                <th style="${thStyle}">Accession / Report</th>
                <th style="${thStyle}">Channel &amp; Endpoint</th>
                <th style="${thStyle}">Status</th>
                <th style="${thStyle}">Payload / Error</th>
                <th style="${thStyle}">Actions</th>
            </tr></thead>
            <tbody>${bodyHtml}</tbody>
        </table>
    </div>`;
}

function openFixCommChannel(dispatchId, curChannel, curDest) {
    let box = document.getElementById('comm-fix-channel-bar');
    if (!box) {
        box = document.createElement('div');
        box.id = 'comm-fix-channel-bar';
        box.style.cssText = 'background:#fef9e7; border:1px solid #f1c40f; border-left:5px solid #e67e22; border-radius:4px; padding:10px 12px; margin:10px 0; display:flex; flex-wrap:wrap; gap:8px; align-items:flex-end; font-size:12px;';
        document.getElementById('main-content').prepend(box);
    }
    const channels = ['FAX', 'HL7_ORU_R01', 'HL7_FHIR_PUSH', 'EMAIL', 'SMS', 'PHONE_TTS', 'PATIENT_PORTAL'];
    box.innerHTML = `
        <div style="font-weight:bold; color:#7d6608; width:100%;">Fix Channel / Endpoint for Dispatch #${dispatchId}</div>
        <div>
            <label style="font-size:11px; font-weight:bold;">Channel</label>
            <select id="fix-comm-ch">
                ${channels.map(c => `<option value="${c}" ${c === curChannel ? 'selected' : ''}>${c}</option>`).join('')}
            </select>
        </div>
        <div style="flex:1; min-width:220px;">
            <label style="font-size:11px; font-weight:bold;">Destination Endpoint (Fax #, Email, Phone, or HL7 Host)</label>
            <input type="text" id="fix-comm-dest" value="${curDest}">
        </div>
        <button class="btn" style="background:#27ae60; margin:0;" onclick="saveAndRequeueCommChannel(${dispatchId})">Save &amp; Re-Queue</button>
        <button class="btn" style="background:#7f8c8d; margin:0;" onclick="document.getElementById('comm-fix-channel-bar')?.remove()">Cancel</button>`;
}

async function previewCommPaperReport(reportId) {
    let overlay = document.getElementById('typst-preview-overlay');
    if (!overlay) {
        overlay = document.createElement('div');
        overlay.id = 'typst-preview-overlay';
        overlay.style.cssText = 'position:fixed; inset:24px; z-index:9999; background:#fff; border:2px solid #2c3e50; border-radius:6px; box-shadow:0 8px 30px rgba(0,0,0,0.45); display:flex; flex-direction:column; overflow:hidden;';
        document.body.appendChild(overlay);
    }

    overlay.innerHTML = `
        <div style="background:#2c3e50; color:#fff; padding:8px 12px; display:flex; justify-content:space-between; align-items:center; font-size:13px;">
            <b>Paper Report Preview (Typst JS Viewer) &mdash; Report #${reportId}</b>
            <button type="button" class="btn btn-danger" style="margin:0; padding:3px 10px; font-size:12px;" onclick="document.getElementById('typst-preview-overlay')?.remove()">Close</button>
        </div>
        <div id="typst-preview-stage" style="flex:1; min-height:0;">Loading Typst report...</div>`;

    try {
        const res = await fetch('/api/report_typ?report_id=' + encodeURIComponent(reportId));
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const typSrc = await res.text();
        if (!window.TypstJS) throw new Error('typst.js is not loaded.');
        TypstJS.mountViewer('typst-preview-stage', typSrc, { filename: `report_${reportId}.pdf` });
    } catch (e) {
        const stage = document.getElementById('typst-preview-stage');
        if (stage) stage.innerHTML = `<div style="padding:16px; color:#c0392b; font-weight:bold;">Failed to render paper report: ${e.message}</div>`;
    }
}
async function saveAndRequeueCommChannel(dispatchId) {
    const ch = document.getElementById('fix-comm-ch')?.value || 'FAX';
    const dest = (document.getElementById('fix-comm-dest')?.value || '').trim();
    if (!dest) return;

    await execSQL(`
        UPDATE correspondence_dispatch_queue
        SET channel = ${sqlStr(ch)},
            destination_endpoint = ${sqlStr(dest)},
            delivery_status = 'QUEUED',
            retry_count = 0,
            error_message = NULL
        WHERE dispatch_id = ${dispatchId}
    `);
    document.getElementById('comm-fix-channel-bar')?.remove();
    if (typeof loadCommsQueue === 'function') loadCommsQueue();
}
function toggleCommAutoRefresh(checked) {
    stopCommAutoRefresh();
    if (checked) {
        g_commAutoRefreshTimer = setInterval(() => {
            if (!document.getElementById('comm-table-wrap')) {
                stopCommAutoRefresh();
                return;
            }
            refreshCommQueue();
        }, 10000);
    }
}

function loadCommQueue() {
    stopCommAutoRefresh();
    g_commExpandedId = null;

    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Communication &amp; Dispatch Queue</h2>
        <div style="display:flex; gap:8px; align-items:center; flex-wrap:wrap;">
            <label style="font-size:12px; font-weight:bold; display:inline-flex; align-items:center; gap:4px; cursor:pointer; background:#ecf0f1; padding:6px 10px; border-radius:4px;">
                <input type="checkbox" id="comm-auto-refresh" style="width:auto; margin:0;" onchange="toggleCommAutoRefresh(this.checked)"> Auto-Refresh (10s)
            </label>
            <button class='btn' style='background:#2ecc71; margin:0;' onclick='dispatchAllQueued()'>Dispatch All Queued</button>
            <button class='btn' style='background:#3498db; margin:0;' onclick='refreshCommQueue()'>Refresh</button>
        </div>
    </div>

    <div id="comm-status-bar" style="display:none; margin-top:10px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div id="patient-contact-banner"></div>
    <div id="comm-kpi-bar" style="display:grid; grid-template-columns:repeat(auto-fit, minmax(140px, 1fr)); gap:10px; margin:12px 0;"></div>

    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px;">
        <div style="flex:2; min-width:200px;">
            <label style="font-size:12px; font-weight:bold;">Search</label>
            <input type="text" id="comm-flt-search" placeholder="Patient, MRN, accession, endpoint, payload, error..." oninput="scheduleCommFilter()">
        </div>
        <div style="min-width:135px;">
            <label style="font-size:12px; font-weight:bold;">Channel</label>
            <select id="comm-flt-channel" onchange="refreshCommQueue()">
                <option value="ALL">All Channels</option>
                <option value="EMAIL">EMAIL</option>
                <option value="SMS">SMS</option>
                <option value="PHONE_TTS">PHONE_TTS</option>
                <option value="PHONE_VERBAL">PHONE_VERBAL</option>
                <option value="HL7_ORU_R01">HL7_ORU_R01</option>
                <option value="HL7_ORM_O01">HL7_ORM_O01</option>
                <option value="HL7_FHIR_PUSH">HL7_FHIR_PUSH</option>
                <option value="FAX">FAX</option>
                <option value="PATIENT_PORTAL">PATIENT_PORTAL</option>
            </select>
        </div>
        <div style="min-width:145px;">
            <label style="font-size:12px; font-weight:bold;">Delivery Status</label>
            <select id="comm-flt-status" onchange="refreshCommQueue()">
                <option value="ALL">All Statuses</option>
                <option value="QUEUED">QUEUED</option>
                <option value="TRANSMITTING">TRANSMITTING</option>
                <option value="SENT">SENT</option>
                <option value="DELIVERED_ACK">DELIVERED_ACK</option>
                <option value="ACKNOWLEDGED">ACKNOWLEDGED</option>
                <option value="FAILED">FAILED</option>
                <option value="FAILED_RETRYING">FAILED_RETRYING</option>
                <option value="PERMANENT_FAILURE">PERMANENT_FAILURE</option>
            </select>
        </div>
        <div style="min-width:135px;">
            <label style="font-size:12px; font-weight:bold;">Recipient Type</label>
            <select id="comm-flt-recip" onchange="refreshCommQueue()">
                <option value="ALL">All Recipients</option>
                <option value="PATIENT">PATIENT</option>
                <option value="REFERRING_MD">REFERRING_MD</option>
                <option value="PRIMARY_CARE_MD">PRIMARY_CARE_MD</option>
                <option value="EXTERNAL_HOSPITAL">EXTERNAL_HOSPITAL</option>
                <option value="INTERNAL_STAFF">INTERNAL_STAFF</option>
            </select>
        </div>
        <div style="min-width:135px;">
            <label style="font-size:12px; font-weight:bold;">Message Type</label>
            <select id="comm-flt-mtype" onchange="refreshCommQueue()">
                <option value="ALL">All Types</option>
                <option value="APPT_REMINDER">APPT_REMINDER</option>
                <option value="FINAL_REPORT">FINAL_REPORT</option>
                <option value="PRELIMINARY_REPORT">PRELIMINARY_REPORT</option>
            </select>
        </div>
        <div style="min-width:125px;">
            <label style="font-size:12px; font-weight:bold;">Queued From</label>
            <input type="date" id="comm-flt-from" onchange="refreshCommQueue()">
        </div>
        <div style="min-width:125px;">
            <label style="font-size:12px; font-weight:bold;">Queued To</label>
            <input type="date" id="comm-flt-to" onchange="refreshCommQueue()">
        </div>
        <div>
            <button class="btn" style="background:#7f8c8d; margin:0;" onclick="clearCommFilters()">Clear</button>
        </div>
    </div>

    <div id="comm-table-wrap">Loading...</div>`;

    refreshCommQueue();
}

function setCommStatusBanner(msg, isError = false) {
    const el = document.getElementById('comm-status-bar');
    if (!el) return;
    if (!msg) { el.style.display = 'none'; return; }
    el.style.display = 'block';
    el.style.background = isError ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isError ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isError ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

function scheduleCommFilter() {
    clearTimeout(g_commFilterTimer);
    g_commFilterTimer = setTimeout(refreshCommQueue, 150);
}

function clearCommFilters() {
    const setVal = (id, v) => { const el = document.getElementById(id); if (el) el.value = v; };
    setVal('comm-flt-search', '');
    setVal('comm-flt-channel', 'ALL');
    setVal('comm-flt-status', 'ALL');
    setVal('comm-flt-recip', 'ALL');
    setVal('comm-flt-mtype', 'ALL');
    setVal('comm-flt-from', '');
    setVal('comm-flt-to', '');
    setCommStatusBanner('');
    refreshCommQueue();
}

function quickFilterComm(statusVal, channelVal = 'ALL') {
    const st = document.getElementById('comm-flt-status');
    const ch = document.getElementById('comm-flt-channel');
    if (st) st.value = statusVal;
    if (ch) ch.value = channelVal;
    refreshCommQueue();
}

function sortCommBy(col) {
    if (g_commSortCol === col) {
        g_commSortAsc = !g_commSortAsc;
    } else {
        g_commSortCol = col;
        g_commSortAsc = true;
    }
    refreshCommQueue();
}

async function refreshCommKpis() {
    const kpiBar = document.getElementById('comm-kpi-bar');
    if (!kpiBar) return;

    const stats = await execSQL(`
        SELECT
            COUNT(*) AS total_cnt,
            SUM(CASE WHEN delivery_status = 'QUEUED' THEN 1 ELSE 0 END) AS queued_cnt,
            SUM(CASE WHEN delivery_status IN ('SENT', 'DELIVERED_ACK', 'ACKNOWLEDGED') THEN 1 ELSE 0 END) AS sent_cnt,
            SUM(CASE WHEN delivery_status IN ('FAILED', 'FAILED_RETRYING', 'PERMANENT_FAILURE') THEN 1 ELSE 0 END) AS failed_cnt,
            SUM(CASE WHEN channel = 'PHONE_VERBAL' AND delivery_status = 'QUEUED' THEN 1 ELSE 0 END) AS verbal_cnt
        FROM correspondence_dispatch_queue
    `);
    if (!Array.isArray(stats) || !stats.length) return;
    const s = stats[0];

    const card = (label, val, color, onClickJs) => `
        <div onclick="${onClickJs}" style="cursor:pointer; background:#fff; border-left:4px solid ${color}; border-radius:4px; padding:10px 12px; box-shadow:0 1px 3px rgba(0,0,0,0.08);">
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">${label}</div>
            <div style="font-size:20px; font-weight:bold; color:${color}; margin-top:2px;">${val || 0}</div>
        </div>`;

    kpiBar.innerHTML =
        card('Total Dispatches', s.total_cnt, '#2c3e50', `quickFilterComm('ALL','ALL')`) +
        card('Queued (Pending)', s.queued_cnt, '#f39c12', `quickFilterComm('QUEUED','ALL')`) +
        card('Sent / Acknowledged', s.sent_cnt, '#27ae60', `quickFilterComm('SENT','ALL')`) +
        card('Failed / Retrying', s.failed_cnt, '#e74c3c', `quickFilterComm('FAILED','ALL')`) +
        card('Verbal Calls Due', s.verbal_cnt, '#8e44ad', `quickFilterComm('QUEUED','PHONE_VERBAL')`);
}

async function refreshCommQueue() {
    const wrap = document.getElementById('comm-table-wrap');
    if (!wrap) return;

    const search = (document.getElementById('comm-flt-search')?.value || '').trim().replace(/'/g, "''");
    const channel = document.getElementById('comm-flt-channel')?.value || 'ALL';
    const status = document.getElementById('comm-flt-status')?.value || 'ALL';
    const recip = document.getElementById('comm-flt-recip')?.value || 'ALL';
    const mtype = document.getElementById('comm-flt-mtype')?.value || 'ALL';
    const fromDt = (document.getElementById('comm-flt-from')?.value || '').trim();
    const toDt = (document.getElementById('comm-flt-to')?.value || '').trim();

    const where = [];
    if (search) {
        where.push(`(
            p.first_name || ' ' || p.last_name LIKE '%${search}%' OR
            p.last_name || ', ' || p.first_name LIKE '%${search}%' OR
            p.mrn LIKE '%${search}%' OR
            COALESCE(o.accession_number, '') LIKE '%${search}%' OR
            q.destination_endpoint LIKE '%${search}%' OR
            q.payload_content LIKE '%${search}%' OR
            COALESCE(q.error_message, '') LIKE '%${search}%'
        )`);
    }
    if (channel !== 'ALL') where.push(`q.channel = '${channel}'`);
    if (status !== 'ALL') where.push(`q.delivery_status = '${status}'`);
    if (recip !== 'ALL') where.push(`q.recipient_type = '${recip}'`);
    if (mtype !== 'ALL') where.push(`q.message_type = '${mtype}'`);
    if (fromDt) where.push(`q.queued_at >= '${fromDt} 00:00:00'`);
    if (toDt) where.push(`q.queued_at <= '${toDt} 23:59:59'`);

    const whereClause = where.length ? `WHERE ${where.join(' AND ')}` : '';
    const allowedSort = new Set([
        'q.dispatch_id', 'q.queued_at', 'q.sent_at', 'q.channel', 'q.recipient_type',
        'patient_name', 'o.accession_number', 'q.message_type', 'q.delivery_status', 'q.retry_count'
    ]);
    const sortCol = allowedSort.has(g_commSortCol) ? g_commSortCol : 'q.queued_at';
    const orderClause = `ORDER BY ${sortCol} ${g_commSortAsc ? 'ASC' : 'DESC'}, q.dispatch_id DESC`;

    const [rows] = await Promise.all([
        execSQL(`
            SELECT q.dispatch_id,
                   q.order_id,
                   q.report_id,
                   q.patient_id,
                   p.mrn,
                   p.last_name || ', ' || p.first_name AS patient_name,
                   COALESCE(o.accession_number, '') AS accession_number,
                   q.recipient_type,
                   q.channel,
                   q.destination_endpoint,
                   q.message_type,
                   q.payload_content,
                   q.delivery_status,
                   COALESCE(q.retry_count, 0) AS retry_count,
                   COALESCE(q.external_ack_id, '') AS external_ack_id,
                   COALESCE(q.error_message, '') AS error_message,
                   q.queued_at,
                   COALESCE(q.sent_at, '') AS sent_at,
                   COALESCE(q.delivered_at, '') AS delivered_at,
                   COALESCE(hu.first_name || ' ' || hu.last_name, 'AUTO') AS handled_by
            FROM correspondence_dispatch_queue q
            JOIN patients p ON q.patient_id = p.patient_id
            LEFT JOIN imaging_orders o ON q.order_id = o.order_id
            LEFT JOIN staff_users hu ON q.handled_by_user_id = hu.user_id
            ${whereClause}
            ${orderClause}
            LIMIT 200
        `),
        refreshCommKpis()
    ]);

    if (rows && rows.error) {
        wrap.innerHTML = `<p style="color:#e74c3c; font-weight:bold;">Database Error: ${rows.error}</p>`;
        return;
    }
    if (!Array.isArray(rows) || rows.length === 0) {
        wrap.innerHTML = '<p style="background:#fff; padding:16px; border-radius:4px;">No communication queue records match the active filters.</p>';
        return;
    }

    g_commLastRows = {};
    rows.forEach(r => { g_commLastRows[r.dispatch_id] = r; });

    const cellStyle = 'padding:6px 5px; font-size:12px; word-break:break-word; overflow-wrap:anywhere; border-bottom:1px solid #ecf0f1;';
    const thStyle = `${cellStyle} background:#3498db; color:#fff; cursor:pointer; border-bottom:2px solid #2980b9; text-align:left;`;
    const btnStyle = 'padding:3px 6px; font-size:11px; margin:1px 2px 1px 0;';

    const th = (col, label, width) => {
        const arrow = g_commSortCol === col ? (g_commSortAsc ? ' &#9650;' : ' &#9660;') : '';
        return `<th style="width:${width}; ${thStyle}" onclick="sortCommBy('${col}')">${label}${arrow}</th>`;
    };

    let html = `
    <div style="font-size:11px; color:#7f8c8d; margin-bottom:4px;">Showing ${rows.length} record(s). Click a row for patient contact banner; click Inspect to view full payload or override status.</div>
    <table style="width:100%; table-layout:fixed; border-collapse:collapse; background:#fff;">
        <thead><tr>
            ${th('q.dispatch_id', 'ID', '5%')}
            ${th('q.queued_at', 'QUEUED', '11%')}
            ${th('q.delivery_status', 'STATUS', '10%')}
            ${th('q.channel', 'CHANNEL', '9%')}
            ${th('q.recipient_type', 'RECIPIENT', '9%')}
            ${th('patient_name', 'PATIENT', '12%')}
            ${th('o.accession_number', 'ACCESSION', '11%')}
            ${th('q.message_type', 'TYPE / ENDPOINT', '14%')}
            ${th('q.retry_count', 'TRIES', '5%')}
            <th style="width:14%; ${thStyle} cursor:default;">ACTIONS</th>
        </tr></thead>
        <tbody>`;

    rows.forEach(r => {
        const stBg = COMM_STATUS_COLORS[r.delivery_status] || '#7f8c8d';
        const chBg = COMM_CHANNEL_COLORS[r.channel] || '#34495e';
        const canAutoSend = AUTO_DISPATCH_CHANNELS.has(r.channel) && !['SENT', 'DELIVERED_ACK', 'ACKNOWLEDGED', 'PERMANENT_FAILURE'].includes(r.delivery_status);
        const isVerbalPending = r.channel === 'PHONE_VERBAL' && r.delivery_status === 'QUEUED';
        const isExpanded = String(g_commExpandedId) === String(r.dispatch_id);

        html += `
        <tr style="cursor:pointer; ${isExpanded ? 'background:#ebf5fb;' : ''}" onclick="renderPatientContactBanner(${r.patient_id})">
            <td style="${cellStyle}">#${r.dispatch_id}</td>
            <td style="${cellStyle}">${escAttr(r.queued_at || '')}${r.sent_at ? `<div style="font-size:10px; color:#27ae60;">Sent: ${escAttr(r.sent_at)}</div>` : ''}</td>
            <td style="${cellStyle}">
                <span style="background:${stBg}; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px; font-weight:bold; display:inline-block;">${r.delivery_status}</span>
                ${r.error_message ? `<div style="font-size:10px; color:#c0392b; margin-top:2px;">${escAttr(r.error_message.substring(0, 60))}</div>` : ''}
            </td>
            <td style="${cellStyle}"><span style="background:${chBg}; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px; font-weight:bold;">${r.channel}</span></td>
            <td style="${cellStyle}">${r.recipient_type}</td>
            <td style="${cellStyle}"><b>${escAttr(r.patient_name)}</b><div style="font-size:10px; color:#7f8c8d;">${escAttr(r.mrn)}</div></td>
            <td style="${cellStyle}">${escAttr(r.accession_number || '-')}</td>
            <td style="${cellStyle}"><b>${escAttr(r.message_type)}</b><div style="font-size:11px; color:#555;">${escAttr(r.destination_endpoint)}</div></td>
            <td style="${cellStyle} text-align:center;">${r.retry_count}</td>
            <td style="${cellStyle}">
                ${canAutoSend ? `<button class='btn' style='background:#2ecc71; ${btnStyle}' onclick='event.stopPropagation(); dispatchQueueItem(${r.dispatch_id})'>${r.delivery_status.startsWith('FAILED') ? 'Retry' : 'Send'}</button>` : ''}
                ${isVerbalPending && r.order_id ? `<button class='btn' style='background:#8e44ad; ${btnStyle}' onclick='event.stopPropagation(); logVerbalCallUI(${r.order_id}, "${escAttr(r.accession_number)}")'>Log Call</button>` : ''}
                <button class='btn' style='background:#34495e; ${btnStyle}' onclick='event.stopPropagation(); toggleCommDetails(${r.dispatch_id})'>${isExpanded ? 'Hide' : 'Inspect'}</button>
            </td>
        </tr>`;

        if (isExpanded) {
            html += `
            <tr style="background:#f8f9fa;">
                <td colspan="10" style="padding:12px; border-bottom:2px solid #3498db; font-size:12px;">
                    <div style="display:flex; flex-wrap:wrap; gap:16px; margin-bottom:10px;">
                        <div><b>Handled By:</b> ${escAttr(r.handled_by)}</div>
                        <div><b>Delivered At:</b> ${escAttr(r.delivered_at || 'N/A')}</div>
                        <div><b>External ACK ID:</b> ${escAttr(r.external_ack_id || 'N/A')}</div>
                        ${r.order_id ? `<div><button class="btn" style="padding:2px 8px; font-size:11px; margin:0; background:#2980b9;" onclick='viewOrderHistory(${r.order_id}, "${escAttr(r.accession_number)}")'>Open Order History</button></div>` : ''}
                    </div>
                    <div style="margin-bottom:8px;">
                        <b>Payload Content:</b>
                        <div style="background:#fff; border:1px solid #ddd; border-radius:4px; padding:8px; margin-top:4px; font-family:monospace; white-space:pre-wrap;">${escAttr(r.payload_content)}</div>
                    </div>
                    ${r.error_message ? `<div style="margin-bottom:8px; color:#c0392b;"><b>Error Diagnostics:</b> ${escAttr(r.error_message)}</div>` : ''}
                    <div style="display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-top:8px;">
                        <label style="font-weight:bold;">Manual Status Override:</label>
                        <select id="comm-ov-status-${r.dispatch_id}" style="width:auto;">
                            ${['QUEUED','TRANSMITTING','SENT','DELIVERED_ACK','ACKNOWLEDGED','FAILED','FAILED_RETRYING','PERMANENT_FAILURE'].map(st =>
                                `<option value="${st}" ${r.delivery_status === st ? 'selected' : ''}>${st}</option>`
                            ).join('')}
                        </select>
                        <button class="btn" style="margin:0; padding:4px 10px; font-size:11px; background:#f39c12;" onclick="updateCommStatus(${r.dispatch_id})">Apply Status</button>
                        <button class="btn btn-danger" style="margin:0; padding:4px 10px; font-size:11px;" onclick="deleteCommItem(${r.dispatch_id})">Delete Entry</button>
                    </div>
                </td>
            </tr>`;
        }
    });

    wrap.innerHTML = html + '</tbody></table>';
}

function toggleCommDetails(dispatchId) {
    g_commExpandedId = (String(g_commExpandedId) === String(dispatchId)) ? null : dispatchId;
    refreshCommQueue();
}

async function dispatchQueueItem(dispatchId) {
    setCommStatusBanner(`Dispatching #${dispatchId}...`);
    const res = await fetch('/api/dispatch_reminder', { method: 'POST', body: String(dispatchId) });
    const raw = await res.text();
    let json = {};
    try { json = raw ? JSON.parse(raw) : {}; } catch (e) {}

    if (!res.ok || json.error) {
        setCommStatusBanner(`Dispatch #${dispatchId} failed: ${json.error || ('HTTP ' + res.status)}`, true);
    } else {
        setCommStatusBanner(`Dispatch #${dispatchId}: ${json.status} (exit code ${json.exit_code ?? 0})`, json.status !== 'SENT');
    }
    refreshCommQueue();
}

async function dispatchAllQueued() {
    const queued = await execSQL(`
        SELECT dispatch_id FROM correspondence_dispatch_queue
        WHERE delivery_status IN ('QUEUED', 'FAILED_RETRYING')
          AND channel IN ('EMAIL', 'SMS', 'PHONE_TTS')
        ORDER BY queued_at ASC
        LIMIT 50
    `);
    if (!Array.isArray(queued) || queued.length === 0) {
        setCommStatusBanner('No QUEUED or FAILED_RETRYING automated (EMAIL/SMS/PHONE_TTS) messages to dispatch.');
        return;
    }

    let okCount = 0, failCount = 0;
    setCommStatusBanner(`Dispatching ${queued.length} queued message(s)...`);
    for (const item of queued) {
        const res = await fetch('/api/dispatch_reminder', { method: 'POST', body: String(item.dispatch_id) });
        const raw = await res.text();
        let json = {};
        try { json = raw ? JSON.parse(raw) : {}; } catch (e) {}
        if (res.ok && json.status === 'SENT') okCount++;
        else failCount++;
    }
    setCommStatusBanner(`Batch complete: ${okCount} sent, ${failCount} failed.`, failCount > 0);
    refreshCommQueue();
}

async function updateCommStatus(dispatchId) {
    const sel = document.getElementById(`comm-ov-status-${dispatchId}`);
    if (!sel) return;
    const newStatus = sel.value;
    const extraTs = (newStatus === 'DELIVERED_ACK' || newStatus === 'ACKNOWLEDGED')
        ? `, delivered_at = COALESCE(delivered_at, datetime('now'))`
        : (newStatus === 'SENT' ? `, sent_at = COALESCE(sent_at, datetime('now'))` : '');

    const res = await execSQL(`
        UPDATE correspondence_dispatch_queue
        SET delivery_status = '${newStatus}'${extraTs}
        WHERE dispatch_id = ${dispatchId}
    `);
    if (res && res.error) setCommStatusBanner(`Update error: ${res.error}`, true);
    else setCommStatusBanner(`Dispatch #${dispatchId} updated to ${newStatus}.`);
    refreshCommQueue();
}

async function deleteCommItem(dispatchId) {
    const res = await execSQL(`DELETE FROM correspondence_dispatch_queue WHERE dispatch_id = ${dispatchId}`);
    if (res && res.error) setCommStatusBanner(`Delete error: ${res.error}`, true);
    else {
        g_commExpandedId = null;
        setCommStatusBanner(`Dispatch #${dispatchId} deleted.`);
        refreshCommQueue();
    }
}