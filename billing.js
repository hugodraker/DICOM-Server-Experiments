/* ============================================================================
 * RIS Revenue Cycle Management (RCM) & Billing Module
 * ============================================================================ */

let g_billTab = 'CLAIMS';
let g_billFilterTimer = null;

function setBillBanner(msg, isError = false) {
    const el = document.getElementById('bill-status-bar');
    if (!el) return;
    if (!msg) { el.style.display = 'none'; return; }
    el.style.display = 'block';
    el.style.background = isError ? '#fdedec' : '#eafaf1';
    el.style.border = `1px solid ${isError ? '#e74c3c' : '#2ecc71'}`;
    el.style.color = isError ? '#922b21' : '#1e8449';
    el.innerText = msg;
}

function moneyFmt(v) {
    const n = Number(v || 0);
    return '$' + (Math.abs(n) < 0.005 ? 0 : n).toFixed(2);
}

async function loadBillingModule(defaultTab = 'CLAIMS') {
    g_billTab = defaultTab;
    document.getElementById('main-content').innerHTML = `
    <div class='header-flex'>
        <h2>Revenue Cycle Management (RCM) &amp; Billing</h2>
        <div style="display:flex; gap:6px; flex-wrap:wrap;">
            <button class="btn" style="background:#27ae60; margin:0;" onclick="newClaimFromOrderUI()">+ New Claim from Order</button>
            <button class="btn" style="background:#2980b9; margin:0;" onclick="newPosReceiptUI()">+ POS Collection</button>
            <button class="btn" style="background:#8e44ad; margin:0;" onclick="editOrderAccidentGrantUI()">WSIB / MVA / Grant</button>
        </div>
    </div>

    <div id="bill-status-bar" style="display:none; margin-top:10px; padding:8px 12px; border-radius:4px; font-size:13px; font-weight:bold;"></div>
    <div id="bill-kpi-bar" style="display:grid; grid-template-columns:repeat(auto-fit, minmax(150px, 1fr)); gap:10px; margin:12px 0;"></div>

    <div style="display:flex; gap:6px; border-bottom:2px solid #3498db; margin-bottom:12px; flex-wrap:wrap;">
        <button id="btab-CLAIMS"   class="btn" style="margin:0; border-radius:4px 4px 0 0;" onclick="switchBillTab('CLAIMS')">Claims &amp; Scrubbing</button>
        <button id="btab-POS"      class="btn" style="margin:0; border-radius:4px 4px 0 0;" onclick="switchBillTab('POS')">Front-Desk POS Receipts</button>
        <button id="btab-FEES"     class="btn" style="margin:0; border-radius:4px 4px 0 0;" onclick="switchBillTab('FEES')">Fee Schedules &amp; Payers</button>
        <button id="btab-BUNDLING" class="btn" style="margin:0; border-radius:4px 4px 0 0;" onclick="switchBillTab('BUNDLING')">NCCI Bundling Rules</button>
    </div>

    <div id="bill-tab-body">Loading...</div>`;

    await refreshBillingKpis();
    switchBillTab(g_billTab);
}

async function refreshBillingKpis() {
    const bar = document.getElementById('bill-kpi-bar');
    if (!bar) return;
    const res = await execSQL(`
        SELECT
            COUNT(*) AS total_claims,
            COALESCE(SUM(total_gross_charges), 0) AS gross_sum,
            COALESCE(SUM(total_payer_paid + total_patient_paid), 0) AS collected_sum,
            COALESCE(SUM(total_contractual_adjustments), 0) AS adj_sum,
            COALESCE(SUM(CASE WHEN claim_status IN ('DRAFT_SCRUBBING','HOLD_CODING_ERROR','DENIED_UNDER_APPEAL') THEN 1 ELSE 0 END), 0) AS action_cnt,
            COALESCE((SELECT SUM(amount_collected) FROM front_desk_pos_collections WHERE date(collected_at) = date('now')), 0) AS pos_today
        FROM billing_claim_headers
    `);
    if (!Array.isArray(res) || !res.length) return;
    const k = res[0];
    const card = (lbl, val, col) => `
        <div style="background:#fff; border-left:4px solid ${col}; border-radius:4px; padding:10px 12px; box-shadow:0 1px 3px rgba(0,0,0,0.08);">
            <div style="font-size:11px; color:#7f8c8d; text-transform:uppercase; font-weight:bold;">${lbl}</div>
            <div style="font-size:18px; font-weight:bold; color:${col}; margin-top:2px;">${val}</div>
        </div>`;
    bar.innerHTML =
        card('Total Claims', k.total_claims, '#2c3e50') +
        card('Gross Charges', moneyFmt(k.gross_sum), '#2980b9') +
        card('Total Collected', moneyFmt(k.collected_sum), '#27ae60') +
        card('Contractual Adj (CO-45)', moneyFmt(k.adj_sum), '#7f8c8d') +
        card('Scrub / Hold / Denied', k.action_cnt, '#e74c3c') +
        card('POS Collected Today', moneyFmt(k.pos_today), '#8e44ad');
}

function switchBillTab(tab) {
    g_billTab = tab;
    ['CLAIMS', 'POS', 'FEES', 'BUNDLING'].forEach(t => {
        const b = document.getElementById(`btab-${t}`);
        if (b) b.style.background = (t === tab) ? '#2980b9' : '#7f8c8d';
    });
    if (tab === 'CLAIMS') renderClaimsTab();
    else if (tab === 'POS') renderPosTab();
    else if (tab === 'FEES') renderFeesTab();
    else if (tab === 'BUNDLING') renderBundlingTab();
}

/* ============================================================================
 * TAB 1: CLAIMS LEDGER, LINE SCRUBBING, ERA REMITTANCE & GL LOCKS
 * ============================================================================ */
function renderClaimsTab() {
    document.getElementById('bill-tab-body').innerHTML = `
    <div style="display:flex; flex-wrap:wrap; gap:10px; align-items:flex-end; background:#fff; padding:12px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:12px;">
        <div style="flex:2; min-width:190px;">
            <label style="font-size:12px; font-weight:bold;">Search Claim / Patient / Accession</label>
            <input type="text" id="clm-flt-search" placeholder="CLM-..., patient name, MRN, accession..." oninput="clearTimeout(g_billFilterTimer); g_billFilterTimer=setTimeout(refreshClaimsTable,150);">
        </div>
        <div style="min-width:170px;">
            <label style="font-size:12px; font-weight:bold;">Claim Status</label>
            <select id="clm-flt-status" onchange="refreshClaimsTable()">
                <option value="ALL">All Claim Statuses</option>
                <option value="DRAFT_SCRUBBING">DRAFT_SCRUBBING</option>
                <option value="HOLD_CODING_ERROR">HOLD_CODING_ERROR</option>
                <option value="READY_FOR_EDI_BATCH">READY_FOR_EDI_BATCH</option>
                <option value="SUBMITTED_PRIMARY">SUBMITTED_PRIMARY</option>
                <option value="PARTIAL_PAID_CROSSOVER">PARTIAL_PAID_CROSSOVER</option>
                <option value="DENIED_UNDER_APPEAL">DENIED_UNDER_APPEAL</option>
                <option value="PATIENT_STATEMENT_SENT">PATIENT_STATEMENT_SENT</option>
                <option value="COLLECTIONS_AGENCY">COLLECTIONS_AGENCY</option>
                <option value="PAID_CLOSED_ZERO_BAL">PAID_CLOSED_ZERO_BAL</option>
            </select>
        </div>
        <div style="min-width:130px;">
            <label style="font-size:12px; font-weight:bold;">Period (YYYY-MM)</label>
            <input type="text" id="clm-flt-period" placeholder="e.g. 2026-11" oninput="clearTimeout(g_billFilterTimer); g_billFilterTimer=setTimeout(refreshClaimsTable,150);">
        </div>
        <div>
            <button class="btn" style="background:#27ae60; margin:0;" onclick="submitReadyEdiBatch()">Submit All Ready EDI</button>
        </div>
    </div>
    <div id="claims-table-wrap">Loading...</div>`;
    refreshClaimsTable();
}

async function refreshClaimsTable() {
    const wrap = document.getElementById('claims-table-wrap');
    if (!wrap) return;
    const search = (document.getElementById('clm-flt-search')?.value || '').trim().replace(/'/g, "''");
    const status = document.getElementById('clm-flt-status')?.value || 'ALL';
    const period = (document.getElementById('clm-flt-period')?.value || '').trim().replace(/'/g, "''");

    const where = [];
    if (status !== 'ALL') where.push(`c.claim_status = '${status}'`);
    if (period) where.push(`c.accounting_period_ym LIKE '${period}%'`);
    if (search) {
        where.push(`(c.claim_control_number LIKE '%${search}%' OR p.first_name || ' ' || p.last_name LIKE '%${search}%' OR p.mrn LIKE '%${search}%' OR o.accession_number LIKE '%${search}%')`);
    }
    const whereSql = where.length ? `WHERE ${where.join(' AND ')}` : '';

    const rows = await execSQL(`
        SELECT c.*, p.mrn, p.last_name || ', ' || p.first_name AS patient_name,
               o.accession_number, bp.payer_code, bp.payer_name
        FROM billing_claim_headers c
        JOIN patients p ON c.patient_id = p.patient_id
        JOIN imaging_orders o ON c.order_id = o.order_id
        JOIN billing_payers bp ON c.primary_payer_id = bp.payer_id
        ${whereSql}
        ORDER BY c.claim_id DESC
        LIMIT 150
    `);

    if (!Array.isArray(rows) || !rows.length) {
        wrap.innerHTML = '<p style="background:#fff; padding:16px; border-radius:4px;">No billing claims match the active filters.</p>';
        return;
    }

    const cell = 'padding:6px 5px; font-size:12px; border-bottom:1px solid #ecf0f1; word-break:break-word;';
    const th = `${cell} background:#2c3e50; color:#fff; text-align:left;`;
    let html = `<table style="width:100%; border-collapse:collapse; background:#fff;">
        <thead><tr>
            <th style="${th}">Claim #</th>
            <th style="${th}">Service / Period</th>
            <th style="${th}">Patient / Accession</th>
            <th style="${th}">Payer</th>
            <th style="${th}">Gross</th>
            <th style="${th}">Expected</th>
            <th style="${th}">Paid</th>
            <th style="${th}">Adj</th>
            <th style="${th}">Pat Bal</th>
            <th style="${th}">Status</th>
            <th style="${th}">Actions</th>
        </tr></thead><tbody>`;

    rows.forEach(r => {
        const stCol = r.claim_status === 'PAID_CLOSED_ZERO_BAL' ? '#27ae60'
            : (r.claim_status.includes('HOLD') || r.claim_status.includes('DENIED') ? '#e74c3c' : '#2980b9');
        html += `<tr>
            <td style="${cell}"><b>${escAttr(r.claim_control_number)}</b>${r.is_locked_gl_closed ? ' <span style="background:#2c3e50;color:#fff;padding:1px 4px;border-radius:2px;font-size:10px;">GL LOCKED</span>' : ''}</td>
            <td style="${cell}">${r.service_date}<div style="font-size:10px;color:#7f8c8d;">Period: ${r.accounting_period_ym}</div></td>
            <td style="${cell}"><b>${escAttr(r.patient_name)}</b><div style="font-size:10px;color:#555;">${escAttr(r.accession_number)} (${escAttr(r.mrn)})</div></td>
            <td style="${cell}"><b>${escAttr(r.payer_code)}</b></td>
            <td style="${cell}">${moneyFmt(r.total_gross_charges)}</td>
            <td style="${cell}">${moneyFmt(r.total_contractual_expected)}</td>
            <td style="${cell}; color:#27ae60; font-weight:bold;">${moneyFmt(Number(r.total_payer_paid) + Number(r.total_patient_paid))}</td>
            <td style="${cell}">${moneyFmt(r.total_contractual_adjustments)}</td>
            <td style="${cell}; font-weight:bold;">${moneyFmt(r.patient_responsibility_balance)}</td>
            <td style="${cell}"><span style="background:${stCol}; color:#fff; padding:2px 5px; border-radius:3px; font-size:10px; font-weight:bold;">${r.claim_status}</span></td>
            <td style="${cell} white-space:nowrap;">
                <button class="btn" style="background:#2980b9; padding:3px 7px; font-size:11px; margin:0 3px 0 0;" onclick="openClaimDetailUI(${r.claim_id})">Lines / ERA</button>
                <button class="btn" style="background:#8e44ad; padding:3px 7px; font-size:11px; margin:0;" onclick="scrubClaim(${r.claim_id})">Scrub</button>
            </td>
        </tr>`;
    });
    wrap.innerHTML = html + '</tbody></table>';
}

async function newClaimFromOrderUI() {
    const [orders, payers] = await Promise.all([
        execSQL(`
            SELECT o.order_id, o.accession_number, o.patient_id, o.facility_id, o.referring_md_id, o.assigned_radiologist_id,
                   o.exam_code, o.prior_auth_number, substr(COALESCE(o.scan_completed_at, o.ordered_at),1,10) AS svc_date,
                   p.last_name, p.first_name, p.default_payer_id, ec.exam_name
            FROM imaging_orders o
            JOIN patients p ON o.patient_id = p.patient_id
            JOIN exam_catalog ec ON o.exam_code = ec.exam_code
            WHERE o.workflow_status NOT IN ('CANCELLED', 'NO_SHOW')
            ORDER BY o.order_id DESC LIMIT 100
        `),
        execSQL(`SELECT payer_id, payer_code, payer_name FROM billing_payers WHERE is_active = 1 ORDER BY payer_name`)
    ]);

    const ordOpts = (Array.isArray(orders) ? orders : []).map(o =>
        `<option value="${o.order_id}" data-payer="${o.default_payer_id || ''}">${escAttr(o.accession_number)} - ${escAttr(o.last_name)}, ${escAttr(o.first_name)} (${escAttr(o.exam_name)}) [${o.svc_date}]</option>`
    ).join('');
    const payOpts = (Array.isArray(payers) ? payers : []).map(p =>
        `<option value="${p.payer_id}">${escAttr(p.payer_code)} - ${escAttr(p.payer_name)}</option>`
    ).join('');

    document.getElementById('bill-tab-body').innerHTML = `
    <div style="background:#fff; padding:18px; max-width:680px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <div class="header-flex" style="margin-bottom:10px;">
            <h3 style="margin:0;">Generate Claim from Imaging Order</h3>
            <button class="btn btn-danger" style="margin:0;" onclick="switchBillTab('CLAIMS')">Cancel</button>
        </div>
        <label style="font-size:12px; font-weight:bold;">Imaging Order *</label>
        <select id="nc-order" style="margin-bottom:10px;">${ordOpts}</select>
        <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:12px;">
            <div>
                <label style="font-size:12px; font-weight:bold;">Primary Payer *</label>
                <select id="nc-payer1">${payOpts}</select>
            </div>
            <div>
                <label style="font-size:12px; font-weight:bold;">Secondary Payer (Optional)</label>
                <select id="nc-payer2"><option value="NULL">-- None --</option>${payOpts}</select>
            </div>
        </div>
        <p style="font-size:12px; color:#555;">Automatically populates Technical (TC), Professional (26), and administered Contrast (J-code / JW waste) lines from <code>fee_schedule_rates</code> and links primary ICD-10 diagnosis codes.</p>
        <button class="btn" style="background:#27ae60;" onclick="createClaimFromOrder()">Generate Claim &amp; Auto-Price Lines</button>
    </div>`;
}

async function createClaimFromOrder() {
    const orderId = document.getElementById('nc-order')?.value;
    const payer1 = document.getElementById('nc-payer1')?.value;
    const payer2 = document.getElementById('nc-payer2')?.value || 'NULL';
    if (!orderId || !payer1) return setBillBanner('Select an order and primary payer.', true);

    const ordRes = await execSQL(`
        SELECT o.*, substr(COALESCE(o.scan_completed_at, o.ordered_at),1,10) AS svc_date,
               COALESCE((SELECT icd10_code FROM order_diagnoses WHERE order_id=o.order_id ORDER BY is_primary DESC LIMIT 1), 'R69') AS icd10
        FROM imaging_orders o WHERE o.order_id = ${orderId}
    `);
    if (!Array.isArray(ordRes) || !ordRes.length) return;
    const o = ordRes[0];

    const ctrlNum = `CLM-${o.svc_date.replace(/-/g, '')}-${String(Date.now()).slice(-4)}`;
    const periodYm = o.svc_date.substring(0, 7);
    const radId = o.assigned_radiologist_id || 'NULL';
    const authNum = o.prior_auth_number ? sqlStr(o.prior_auth_number) : 'NULL';

    const ins = await execSQL(`
        INSERT INTO billing_claim_headers
        (claim_control_number, order_id, patient_id, facility_id, primary_payer_id, secondary_payer_id, rendering_radiologist_id, referring_md_id, prior_auth_number, service_date, accounting_period_ym)
        VALUES ('${ctrlNum}', ${o.order_id}, ${o.patient_id}, ${o.facility_id}, ${payer1}, ${payer2}, ${radId}, ${o.referring_md_id}, ${authNum}, '${o.svc_date}', '${periodYm}')
    `);
    if (ins && ins.error) return setBillBanner(ins.error, true);

    const idRes = await execSQL(`SELECT claim_id FROM billing_claim_headers WHERE claim_control_number = '${ctrlNum}'`);
    const claimId = idRes?.[0]?.claim_id;
    if (!claimId) return;

    // Auto-populate charge lines from fee_schedule_rates matching exam_code (or all active rates for payer if exam_code uses provincial code)
    const rates = await execSQL(`
        SELECT * FROM fee_schedule_rates
        WHERE payer_id = ${payer1} AND billing_code = ${sqlStr(o.exam_code)}
          AND (effective_end_date IS NULL OR effective_end_date >= '${o.svc_date}')
        ORDER BY rate_id
    `);

    let lineNum = 1;
    if (Array.isArray(rates) && rates.length > 0) {
        for (const rt of rates) {
            const cat = rt.component_modifier === 'TC' ? 'PROCEDURE_TC'
                : (rt.component_modifier === '26' ? 'PROCEDURE_PC_26' : 'PROCEDURE_GLOBAL');
            const mod1 = (rt.component_modifier === 'TC' || rt.component_modifier === '26') ? `'${rt.component_modifier}'` : 'NULL';
            await execSQL(`
                INSERT INTO billing_claim_lines
                (claim_id, line_number, charge_category, billing_code, modifier_1, icd10_pointer, units_quantity, unit_gross_charge, line_gross_total, line_expected_allowed, work_rvu_earned)
                VALUES (${claimId}, ${lineNum++}, '${cat}', ${sqlStr(rt.billing_code)}, ${mod1}, ${sqlStr(o.icd10)}, 1, ${rt.gross_charge_amount}, ${rt.gross_charge_amount}, ${rt.contractual_allowed_amount}, ${rt.work_rvu || 0})
            `);
        }
    }

    // Auto-add contrast lines if contrast_and_medication_logs exist
    const contrasts = await execSQL(`SELECT volume_ml FROM contrast_and_medication_logs WHERE order_id = ${o.order_id} AND agent_category IN ('IODINATED_CONTRAST','GADOLINIUM_CONTRAST')`);
    if (Array.isArray(contrasts) && contrasts.length > 0) {
        const vol = Number(contrasts[0].volume_ml || 0);
        if (vol > 0) {
            await execSQL(`
                INSERT INTO billing_claim_lines
                (claim_id, line_number, charge_category, billing_code, icd10_pointer, units_quantity, unit_gross_charge, line_gross_total, line_expected_allowed)
                VALUES (${claimId}, ${lineNum++}, 'CONTRAST_HCPCS_JCODE', 'Q9967', ${sqlStr(o.icd10)}, ${vol}, 2.00, ${(vol * 2).toFixed(2)}, ${(vol * 0.45).toFixed(2)})
            `);
        }
    }

    await execSQL(`UPDATE imaging_orders SET billing_status = 'BILLED' WHERE order_id = ${o.order_id}`);
    await refreshBillingKpis();
    setBillBanner(`Created claim ${ctrlNum}.`);
    openClaimDetailUI(claimId);
}

async function openClaimDetailUI(claimId) {
    const [hdrRes, linesRes, remRes] = await Promise.all([
        execSQL(`
            SELECT c.*, p.mrn, p.first_name, p.last_name, o.accession_number, bp.payer_code, bp.payer_name
            FROM billing_claim_headers c
            JOIN patients p ON c.patient_id = p.patient_id
            JOIN imaging_orders o ON c.order_id = o.order_id
            JOIN billing_payers bp ON c.primary_payer_id = bp.payer_id
            WHERE c.claim_id = ${claimId}
        `),
        execSQL(`SELECT * FROM billing_claim_lines WHERE claim_id = ${claimId} ORDER BY line_number ASC`),
        execSQL(`SELECT * FROM billing_remittance_transactions WHERE claim_id = ${claimId} ORDER BY posted_at DESC, tx_id DESC`)
    ]);
    if (!Array.isArray(hdrRes) || !hdrRes.length) return;
    const c = hdrRes[0];
    const lines = Array.isArray(linesRes) ? linesRes : [];
    const rems = Array.isArray(remRes) ? remRes : [];
    const locked = !!c.is_locked_gl_closed;

    const lineRows = lines.length ? lines.map(l => `
        <tr>
            <td style="padding:5px;">${l.line_number}</td>
            <td style="padding:5px;">${l.charge_category}</td>
            <td style="padding:5px;"><b>${escAttr(l.billing_code)}</b> ${escAttr(l.modifier_1 || '')} ${escAttr(l.modifier_2 || '')}</td>
            <td style="padding:5px;">${escAttr(l.icd10_pointer)}</td>
            <td style="padding:5px;">${l.units_quantity}</td>
            <td style="padding:5px;">${moneyFmt(l.unit_gross_charge)}</td>
            <td style="padding:5px;">${moneyFmt(l.line_gross_total)}</td>
            <td style="padding:5px;">${moneyFmt(l.line_expected_allowed)}</td>
            <td style="padding:5px;">${l.work_rvu_earned}</td>
            <td style="padding:5px;">${!locked ? `<button class="btn btn-danger" style="padding:2px 6px;font-size:11px;margin:0;" onclick="deleteClaimLine(${claimId}, ${l.line_id})">Del</button>` : ''}</td>
        </tr>`).join('') : '<tr><td colspan="10" style="padding:8px;">No charge lines on claim.</td></tr>';

    const remRows = rems.length ? rems.map(t => `
        <tr>
            <td style="padding:5px;">${t.posted_at}</td>
            <td style="padding:5px;">${t.tx_source}</td>
            <td style="padding:5px;"><b>${t.tx_type}</b></td>
            <td style="padding:5px;">${moneyFmt(t.amount)}</td>
            <td style="padding:5px;">${escAttr(t.carc_reason_code || '')} ${escAttr(t.rarc_remark_code || '')}</td>
            <td style="padding:5px;">${escAttr(t.check_or_eft_trace_number || '')}</td>
        </tr>`).join('') : '<tr><td colspan="6" style="padding:8px;">No remittance transactions posted yet.</td></tr>';

    document.getElementById('bill-tab-body').innerHTML = `
    <div style="background:#fff; padding:16px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:13px;">
        <div class="header-flex" style="margin-bottom:10px;">
            <h3 style="margin:0;">Claim ${escAttr(c.claim_control_number)} &mdash; ${escAttr(c.last_name)}, ${escAttr(c.first_name)} (${escAttr(c.accession_number)})</h3>
            <div style="display:flex; gap:6px; flex-wrap:wrap;">
                ${!locked ? `<button class="btn" style="background:#8e44ad; margin:0;" onclick="scrubClaim(${claimId}, true)">Run NCCI Scrub</button>` : ''}
                ${!locked ? `<button class="btn" style="background:#2980b9; margin:0;" onclick="updateClaimStatus(${claimId}, 'SUBMITTED_PRIMARY')">Submit Primary EDI</button>` : ''}
                ${!locked ? `<button class="btn" style="background:#34495e; margin:0;" onclick="lockClaimGlPeriod(${claimId})">Lock GL Period</button>` : ''}
                <button class="btn" style="background:#7f8c8d; margin:0;" onclick="switchBillTab('CLAIMS')">&larr; Back</button>
            </div>
        </div>

        <div style="display:flex; flex-wrap:wrap; gap:18px; background:#f8f9fa; padding:10px; border-radius:4px; margin-bottom:12px;">
            <div><b>Payer:</b> ${escAttr(c.payer_name)}</div>
            <div><b>Status:</b> ${c.claim_status}</div>
            <div><b>Gross:</b> ${moneyFmt(c.total_gross_charges)}</div>
            <div><b>Expected Allowed:</b> ${moneyFmt(c.total_contractual_expected)}</div>
            <div><b>Payer Paid:</b> ${moneyFmt(c.total_payer_paid)}</div>
            <div><b>Patient Paid:</b> ${moneyFmt(c.total_patient_paid)}</div>
            <div><b>Adjustments:</b> ${moneyFmt(c.total_contractual_adjustments)}</div>
            <div><b>Patient Bal:</b> ${moneyFmt(c.patient_responsibility_balance)}</div>
        </div>

        <h4 style="margin:10px 0 6px 0; border-bottom:1px solid #ddd; padding-bottom:4px;">Charge Lines</h4>
        <table style="width:100%; border-collapse:collapse; font-size:12px; margin-bottom:10px;">
            <thead><tr style="background:#34495e; color:#fff;">
                <th style="padding:5px; text-align:left;">#</th><th style="padding:5px; text-align:left;">Category</th><th style="padding:5px; text-align:left;">Code / Mods</th>
                <th style="padding:5px; text-align:left;">ICD-10</th><th style="padding:5px; text-align:left;">Units</th><th style="padding:5px; text-align:left;">Unit $</th>
                <th style="padding:5px; text-align:left;">Gross</th><th style="padding:5px; text-align:left;">Allowed</th><th style="padding:5px; text-align:left;">wRVU</th><th></th>
            </tr></thead>
            <tbody>${lineRows}</tbody>
        </table>

        ${!locked ? `
        <div style="display:grid; grid-template-columns:1.3fr 0.8fr 0.6fr 0.7fr 0.6fr 0.7fr 0.7fr 0.6fr auto; gap:6px; align-items:end; background:#eef5fb; padding:8px; border-radius:4px; margin-bottom:16px;">
            <div><label style="font-size:10px;font-weight:bold;">Category</label>
                <select id="cl-cat">
                    <option value="PROCEDURE_TC">PROCEDURE_TC</option>
                    <option value="PROCEDURE_PC_26">PROCEDURE_PC_26</option>
                    <option value="PROCEDURE_GLOBAL">PROCEDURE_GLOBAL</option>
                    <option value="CONTRAST_HCPCS_JCODE">CONTRAST_HCPCS_JCODE</option>
                    <option value="CONTRAST_WASTE_JW">CONTRAST_WASTE_JW</option>
                    <option value="AFTER_HOURS_SURCHARGE">AFTER_HOURS_SURCHARGE</option>
                    <option value="IV_SEDATION_SUPPLY">IV_SEDATION_SUPPLY</option>
                </select>
            </div>
            <div><label style="font-size:10px;font-weight:bold;">Code *</label><input type="text" id="cl-code" placeholder="e.g. X406"></div>
            <div><label style="font-size:10px;font-weight:bold;">Mod 1</label><input type="text" id="cl-mod1" placeholder="TC/26/59"></div>
            <div><label style="font-size:10px;font-weight:bold;">ICD-10 *</label><input type="text" id="cl-icd" value="R07.9"></div>
            <div><label style="font-size:10px;font-weight:bold;">Units</label><input type="number" step="0.01" id="cl-units" value="1"></div>
            <div><label style="font-size:10px;font-weight:bold;">Unit Gross $</label><input type="number" step="0.01" id="cl-gross" value="0"></div>
            <div><label style="font-size:10px;font-weight:bold;">Allowed $</label><input type="number" step="0.01" id="cl-allow" value="0"></div>
            <div><label style="font-size:10px;font-weight:bold;">wRVU</label><input type="number" step="0.01" id="cl-rvu" value="0"></div>
            <button class="btn" style="margin:0; padding:6px 10px;" onclick="addClaimLine(${claimId}, ${lines.length + 1})">+ Add</button>
        </div>` : ''}

        <h4 style="margin:10px 0 6px 0; border-bottom:1px solid #ddd; padding-bottom:4px;">Remittance &amp; ERA 835 Ledger</h4>
        <table style="width:100%; border-collapse:collapse; font-size:12px; margin-bottom:10px;">
            <thead><tr style="background:#2c3e50; color:#fff;">
                <th style="padding:5px; text-align:left;">Posted At</th><th style="padding:5px; text-align:left;">Source</th><th style="padding:5px; text-align:left;">Type</th>
                <th style="padding:5px; text-align:left;">Amount</th><th style="padding:5px; text-align:left;">CARC / RARC</th><th style="padding:5px; text-align:left;">Check / EFT Trace</th>
            </tr></thead>
            <tbody>${remRows}</tbody>
        </table>

        ${!locked ? `
        <div style="display:grid; grid-template-columns:1.2fr 1.3fr 0.8fr 0.7fr 1fr auto; gap:6px; align-items:end; background:#eafaf1; padding:8px; border-radius:4px;">
            <div><label style="font-size:10px;font-weight:bold;">Source</label>
                <select id="rm-src">
                    <option value="PRIMARY_PAYER_ERA_835">PRIMARY_PAYER_ERA_835</option>
                    <option value="SECONDARY_PAYER_EOB">SECONDARY_PAYER_EOB</option>
                    <option value="PATIENT_POS_RECEIPT">PATIENT_POS_RECEIPT</option>
                    <option value="ADMIN_WRITE_OFF">ADMIN_WRITE_OFF</option>
                </select>
            </div>
            <div><label style="font-size:10px;font-weight:bold;">Transaction Type</label>
                <select id="rm-type">
                    <option value="PAYMENT">PAYMENT</option>
                    <option value="CONTRACTUAL_ADJUSTMENT_CO45">CONTRACTUAL_ADJUSTMENT_CO45</option>
                    <option value="MPPR_REDUCTION_ADJUSTMENT">MPPR_REDUCTION_ADJUSTMENT</option>
                    <option value="DEDUCTIBLE_TRANSFER_PR1">DEDUCTIBLE_TRANSFER_PR1</option>
                    <option value="COINSURANCE_TRANSFER_PR2">COINSURANCE_TRANSFER_PR2</option>
                    <option value="COPAY_TRANSFER_PR3">COPAY_TRANSFER_PR3</option>
                    <option value="DENIAL_REVERSAL">DENIAL_REVERSAL</option>
                    <option value="REFUND_TO_PATIENT_OR_PAYER">REFUND_TO_PATIENT_OR_PAYER</option>
                </select>
            </div>
            <div><label style="font-size:10px;font-weight:bold;">Amount ($) *</label><input type="number" step="0.01" id="rm-amt" placeholder="0.00"></div>
            <div><label style="font-size:10px;font-weight:bold;">CARC</label><input type="text" id="rm-carc" placeholder="CO-45"></div>
            <div><label style="font-size:10px;font-weight:bold;">EFT / Check #</label><input type="text" id="rm-trace" placeholder="EFT-..."></div>
            <button class="btn" style="background:#27ae60; margin:0; padding:6px 10px;" onclick="postClaimRemittance(${claimId})">Post Tx</button>
        </div>` : ''}
    </div>`;
}

async function addClaimLine(claimId, nextNum) {
    const cat = document.getElementById('cl-cat').value;
    const code = document.getElementById('cl-code').value.trim();
    const mod1 = document.getElementById('cl-mod1').value.trim();
    const icd = document.getElementById('cl-icd').value.trim();
    const units = Number(document.getElementById('cl-units').value || 1);
    const gross = Number(document.getElementById('cl-gross').value || 0);
    const allow = Number(document.getElementById('cl-allow').value || 0);
    const rvu = Number(document.getElementById('cl-rvu').value || 0);
    if (!code || !icd) return setBillBanner('Billing code and ICD-10 pointer are required.', true);

    const res = await execSQL(`
        INSERT INTO billing_claim_lines
        (claim_id, line_number, charge_category, billing_code, modifier_1, icd10_pointer, units_quantity, unit_gross_charge, line_gross_total, line_expected_allowed, work_rvu_earned)
        VALUES (${claimId}, ${nextNum}, '${cat}', ${sqlStr(code)}, ${sqlStr(mod1, true)}, ${sqlStr(icd)}, ${units}, ${gross}, ${(units * gross).toFixed(2)}, ${allow}, ${rvu})
    `);
    if (res && res.error) return setBillBanner(res.error, true);
    await recalcClaimBalances(claimId);
    openClaimDetailUI(claimId);
}

async function deleteClaimLine(claimId, lineId) {
    const res = await execSQL(`DELETE FROM billing_claim_lines WHERE line_id = ${lineId}`);
    if (res && res.error) return setBillBanner(res.error, true);
    await recalcClaimBalances(claimId);
    openClaimDetailUI(claimId);
}

async function recalcClaimBalances(claimId) {
    await execSQL(`
        UPDATE billing_claim_headers
        SET total_gross_charges = COALESCE((SELECT SUM(line_gross_total) FROM billing_claim_lines WHERE claim_id = ${claimId}), 0),
            total_contractual_expected = COALESCE((SELECT SUM(line_expected_allowed) FROM billing_claim_lines WHERE claim_id = ${claimId}), 0),
            remaining_total_balance = ROUND(
                COALESCE((SELECT SUM(line_gross_total) FROM billing_claim_lines WHERE claim_id = ${claimId}), 0)
                - total_payer_paid - total_patient_paid - total_contractual_adjustments, 2)
        WHERE claim_id = ${claimId} AND is_locked_gl_closed = 0
    `);
    await refreshBillingKpis();
}

async function postClaimRemittance(claimId) {
    const src = document.getElementById('rm-src').value;
    const type = document.getElementById('rm-type').value;
    const amt = Number(document.getElementById('rm-amt').value || 0);
    const carc = document.getElementById('rm-carc').value.trim();
    const trace = document.getElementById('rm-trace').value.trim();
    if (!amt) return setBillBanner('Enter a non-zero remittance amount.', true);

    const res = await execSQL(`
        INSERT INTO billing_remittance_transactions (claim_id, tx_source, tx_type, amount, carc_reason_code, check_or_eft_trace_number)
        VALUES (${claimId}, '${src}', '${type}', ${amt.toFixed(2)}, ${sqlStr(carc, true)}, ${sqlStr(trace, true)})
    `);
    if (res && res.error) return setBillBanner(res.error, true);

    await recalcClaimBalances(claimId);
    // Re-check zero balance auto-close after remaining_total_balance recomputation
    await execSQL(`
        UPDATE billing_claim_headers
        SET claim_status = 'PAID_CLOSED_ZERO_BAL', closed_at = COALESCE(closed_at, datetime('now'))
        WHERE claim_id = ${claimId} AND ABS(remaining_total_balance) < 0.01 AND total_gross_charges > 0 AND is_locked_gl_closed = 0
    `);
    await refreshBillingKpis();
    setBillBanner('Remittance transaction posted.');
    openClaimDetailUI(claimId);
}

async function scrubClaim(claimId, stayOnDetail = false) {
    const [lines, rules] = await Promise.all([
        execSQL(`SELECT * FROM billing_claim_lines WHERE claim_id = ${claimId} ORDER BY line_number`),
        execSQL(`SELECT * FROM billing_bundling_rules`)
    ]);
    if (!Array.isArray(lines) || !lines.length) {
        await updateClaimStatus(claimId, 'HOLD_CODING_ERROR');
        setBillBanner(`Claim #${claimId} held: no charge lines present.`, true);
        if (stayOnDetail) openClaimDetailUI(claimId); else refreshClaimsTable();
        return;
    }

    const errors = [];
    const codes = lines.map(l => l.billing_code);
    (Array.isArray(rules) ? rules : []).forEach(r => {
        const lineB = lines.find(l => l.billing_code === r.code_b);
        if (codes.includes(r.code_a) && lineB) {
            if (r.rule_action === 'DENY_CODE_B_INCLUSIVE') {
                errors.push(`NCCI Edit: ${r.code_b} is inclusive to ${r.code_a}.`);
            } else if (r.rule_action === 'REQUIRE_MODIFIER_59' && lineB.modifier_1 !== '59' && lineB.modifier_2 !== '59') {
                errors.push(`NCCI Edit: ${r.code_b} requires Modifier 59 when billed with ${r.code_a}.`);
            } else if (r.rule_action === 'COMBINE_INTO_TARGET') {
                errors.push(`NCCI Edit: Combine ${r.code_a} + ${r.code_b} into ${r.target_combined_code || 'target code'}.`);
            }
        }
    });

    const newStatus = errors.length ? 'HOLD_CODING_ERROR' : 'READY_FOR_EDI_BATCH';
    await updateClaimStatus(claimId, newStatus);
    setBillBanner(errors.length ? `Claim #${claimId} HELD: ${errors.join(' ')}` : `Claim #${claimId} passed NCCI scrub -> READY_FOR_EDI_BATCH.`, errors.length > 0);
    if (stayOnDetail) openClaimDetailUI(claimId); else refreshClaimsTable();
}

async function updateClaimStatus(claimId, status) {
    const extra = status === 'SUBMITTED_PRIMARY' ? `, submitted_at = COALESCE(submitted_at, datetime('now'))` : '';
    const res = await execSQL(`UPDATE billing_claim_headers SET claim_status = '${status}'${extra} WHERE claim_id = ${claimId}`);
    if (res && res.error) setBillBanner(res.error, true);
    await refreshBillingKpis();
}

async function submitReadyEdiBatch() {
    const res = await execSQL(`
        UPDATE billing_claim_headers
        SET claim_status = 'SUBMITTED_PRIMARY', submitted_at = datetime('now')
        WHERE claim_status = 'READY_FOR_EDI_BATCH' AND is_locked_gl_closed = 0
    `);
    if (res && res.error) return setBillBanner(res.error, true);
    setBillBanner('All READY_FOR_EDI_BATCH claims submitted to primary clearinghouse.');
    refreshClaimsTable();
    refreshBillingKpis();
}

async function lockClaimGlPeriod(claimId) {
    const res = await execSQL(`UPDATE billing_claim_headers SET is_locked_gl_closed = 1 WHERE claim_id = ${claimId}`);
    if (res && res.error) return setBillBanner(res.error, true);
    setBillBanner(`Claim #${claimId} General Ledger period locked (immutable).`);
    openClaimDetailUI(claimId);
}

/* ============================================================================
 * TAB 2: FRONT-DESK POS COLLECTIONS (front_desk_pos_collections)
 * ============================================================================ */
async function renderPosTab() {
    const rows = await execSQL(`
        SELECT r.*, p.mrn, p.last_name || ', ' || p.first_name AS patient_name,
               COALESCE(o.accession_number, '-') AS accession_number,
               COALESCE(c.claim_control_number, '-') AS claim_num
        FROM front_desk_pos_collections r
        JOIN patients p ON r.patient_id = p.patient_id
        LEFT JOIN imaging_orders o ON r.order_id = o.order_id
        LEFT JOIN billing_claim_headers c ON r.claim_id = c.claim_id
        ORDER BY r.collected_at DESC LIMIT 100
    `);

    const cell = 'padding:6px; font-size:12px; border-bottom:1px solid #ecf0f1;';
    const th = `${cell} background:#2c3e50; color:#fff; text-align:left;`;
    let html = `<table style="width:100%; border-collapse:collapse; background:#fff;">
        <thead><tr>
            <th style="${th}">Receipt #</th><th style="${th}">Date</th><th style="${th}">Patient</th>
            <th style="${th}">Accession / Claim</th><th style="${th}">Purpose</th><th style="${th}">Method</th>
            <th style="${th}">Amount</th><th style="${th}">Auth Code</th>
        </tr></thead><tbody>`;

    if (!Array.isArray(rows) || !rows.length) {
        html += `<tr><td colspan="8" style="padding:12px;">No POS collection receipts recorded yet.</td></tr>`;
    } else {
        rows.forEach(r => {
            html += `<tr>
                <td style="${cell}"><b>${escAttr(r.receipt_number)}</b></td>
                <td style="${cell}">${r.collected_at}</td>
                <td style="${cell}">${escAttr(r.patient_name)} (${escAttr(r.mrn)})</td>
                <td style="${cell}">${escAttr(r.accession_number)} / ${escAttr(r.claim_num)}</td>
                <td style="${cell}">${r.collection_purpose}</td>
                <td style="${cell}">${r.payment_method}</td>
                <td style="${cell}; color:#27ae60; font-weight:bold;">${moneyFmt(r.amount_collected)}</td>
                <td style="${cell}">${escAttr(r.terminal_auth_code || '')}</td>
            </tr>`;
        });
    }
    document.getElementById('bill-tab-body').innerHTML = html + '</tbody></table>';
}

async function newPosReceiptUI() {
    const [pats, orders, claims] = await Promise.all([
        execSQL(`SELECT patient_id, mrn, last_name, first_name FROM patients ORDER BY last_name`),
        execSQL(`SELECT order_id, accession_number, patient_id FROM imaging_orders ORDER BY order_id DESC LIMIT 100`),
        execSQL(`SELECT claim_id, claim_control_number, patient_id FROM billing_claim_headers WHERE is_locked_gl_closed = 0 ORDER BY claim_id DESC LIMIT 100`)
    ]);

    const patOpts = (Array.isArray(pats) ? pats : []).map(p => `<option value="${p.patient_id}">${escAttr(p.last_name)}, ${escAttr(p.first_name)} (${escAttr(p.mrn)})</option>`).join('');
    const ordOpts = `<option value="NULL">-- None --</option>` + (Array.isArray(orders) ? orders : []).map(o => `<option value="${o.order_id}">${escAttr(o.accession_number)}</option>`).join('');
    const clmOpts = `<option value="NULL">-- None --</option>` + (Array.isArray(claims) ? claims : []).map(c => `<option value="${c.claim_id}">${escAttr(c.claim_control_number)}</option>`).join('');

    document.getElementById('bill-tab-body').innerHTML = `
    <div style="background:#fff; padding:18px; max-width:650px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:13px;">
        <div class="header-flex" style="margin-bottom:10px;">
            <h3 style="margin:0;">Record Front-Desk POS Payment</h3>
            <button class="btn btn-danger" style="margin:0;" onclick="switchBillTab('POS')">Cancel</button>
        </div>
        <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px;">
            <div><label style="font-size:12px;font-weight:bold;">Receipt # *</label><input type="text" id="pos-num" value="RCP-${Date.now().toString().slice(-6)}"></div>
            <div><label style="font-size:12px;font-weight:bold;">Patient *</label><select id="pos-pat">${patOpts}</select></div>
            <div><label style="font-size:12px;font-weight:bold;">Linked Order</label><select id="pos-ord">${ordOpts}</select></div>
            <div><label style="font-size:12px;font-weight:bold;">Linked Claim (Auto-posts Remittance)</label><select id="pos-clm">${clmOpts}</select></div>
            <div><label style="font-size:12px;font-weight:bold;">Purpose</label>
                <select id="pos-purp">
                    <option value="COPAY">COPAY</option>
                    <option value="DEDUCTIBLE_ESTIMATE">DEDUCTIBLE_ESTIMATE</option>
                    <option value="SELF_PAY_PACKAGE">SELF_PAY_PACKAGE</option>
                    <option value="CD_FILM_COPY_FEE">CD_FILM_COPY_FEE</option>
                    <option value="PRIOR_BALANCE">PRIOR_BALANCE</option>
                </select>
            </div>
            <div><label style="font-size:12px;font-weight:bold;">Payment Method</label>
                <select id="pos-meth">
                    <option value="DEBIT_INTERAC">DEBIT_INTERAC</option>
                    <option value="CREDIT_CARD">CREDIT_CARD</option>
                    <option value="CASH">CASH</option>
                    <option value="CHEQUE">CHEQUE</option>
                    <option value="PATIENT_PORTAL_STRIPE">PATIENT_PORTAL_STRIPE</option>
                </select>
            </div>
            <div><label style="font-size:12px;font-weight:bold;">Amount Collected ($) *</label><input type="number" step="0.01" id="pos-amt" placeholder="50.00"></div>
            <div><label style="font-size:12px;font-weight:bold;">Terminal Auth Code</label><input type="text" id="pos-auth" placeholder="AUTH-..."></div>
        </div>
        <button class="btn" style="background:#27ae60;" onclick="submitPosReceipt()">Save Receipt</button>
    </div>`;
}

async function submitPosReceipt() {
    const num = document.getElementById('pos-num').value.trim();
    const patId = document.getElementById('pos-pat').value;
    const ordId = document.getElementById('pos-ord').value || 'NULL';
    const clmId = document.getElementById('pos-clm').value || 'NULL';
    const purp = document.getElementById('pos-purp').value;
    const meth = document.getElementById('pos-meth').value;
    const amt = Number(document.getElementById('pos-amt').value || 0);
    const auth = document.getElementById('pos-auth').value.trim();
    if (!num || !patId || amt <= 0) return setBillBanner('Receipt #, patient, and positive amount are required.', true);

    const staffRes = await execSQL(`SELECT user_id FROM staff_users WHERE is_active = 1 LIMIT 1`);
    const collectorId = staffRes?.[0]?.user_id || 1;

    const res = await execSQL(`
        INSERT INTO front_desk_pos_collections
        (receipt_number, patient_id, order_id, claim_id, collection_purpose, payment_method, amount_collected, terminal_auth_code, collected_by_user_id)
        VALUES (${sqlStr(num)}, ${patId}, ${ordId}, ${clmId}, '${purp}', '${meth}', ${amt.toFixed(2)}, ${sqlStr(auth, true)}, ${collectorId})
    `);
    if (res && res.error) return setBillBanner(res.error, true);

    if (clmId !== 'NULL') {
        await execSQL(`
            INSERT INTO billing_remittance_transactions (claim_id, tx_source, tx_type, amount, check_or_eft_trace_number, posted_by_user_id)
            VALUES (${clmId}, 'PATIENT_POS_RECEIPT', 'PAYMENT', ${amt.toFixed(2)}, ${sqlStr(num)}, ${collectorId})
        `);
        await recalcClaimBalances(clmId);
    }

    await refreshBillingKpis();
    setBillBanner(`POS receipt ${num} (${moneyFmt(amt)}) recorded.`);
    switchBillTab('POS');
}

/* ============================================================================
 * WSIB / MVA ACCIDENT & RESEARCH GRANT INFO (order_billing_accident_grant_info)
 * ============================================================================ */
async function editOrderAccidentGrantUI() {
    const orders = await execSQL(`
        SELECT o.order_id, o.accession_number, p.last_name, p.first_name, g.*
        FROM imaging_orders o
        JOIN patients p ON o.patient_id = p.patient_id
        LEFT JOIN order_billing_accident_grant_info g ON o.order_id = g.order_id
        ORDER BY o.order_id DESC LIMIT 100
    `);
    const ordOpts = (Array.isArray(orders) ? orders : []).map(o =>
        `<option value="${o.order_id}">${escAttr(o.accession_number)} - ${escAttr(o.last_name)}, ${escAttr(o.first_name)}${o.billing_category ? ' [' + o.billing_category + ']' : ''}</option>`
    ).join('');

    document.getElementById('bill-tab-body').innerHTML = `
    <div style="background:#fff; padding:18px; max-width:680px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); font-size:13px;">
        <div class="header-flex" style="margin-bottom:10px;">
            <h3 style="margin:0;">Order Accident (WSIB / MVA) &amp; Research Grant Info</h3>
            <button class="btn btn-danger" style="margin:0;" onclick="switchBillTab('CLAIMS')">Cancel</button>
        </div>
        <label style="font-size:12px;font-weight:bold;">Order *</label>
        <select id="ag-order" style="margin-bottom:10px;">${ordOpts}</select>
        <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; margin-bottom:10px;">
            <div><label style="font-size:12px;font-weight:bold;">Billing Category *</label>
                <select id="ag-cat">
                    <option value="STANDARD_MEDICAL">STANDARD_MEDICAL</option>
                    <option value="WORKERS_COMP_WSIB">WORKERS_COMP_WSIB</option>
                    <option value="AUTO_ACCIDENT_MVA">AUTO_ACCIDENT_MVA</option>
                    <option value="RESEARCH_STUDY_GRANT">RESEARCH_STUDY_GRANT</option>
                    <option value="LEGAL_IME">LEGAL_IME</option>
                </select>
            </div>
            <div><label style="font-size:12px;font-weight:bold;">Date of Injury / Accident</label><input type="date" id="ag-doi"></div>
            <div><label style="font-size:12px;font-weight:bold;">WSIB / MVA Claim #</label><input type="text" id="ag-clm"></div>
            <div><label style="font-size:12px;font-weight:bold;">Employer / Auto Insurer Name</label><input type="text" id="ag-emp"></div>
            <div><label style="font-size:12px;font-weight:bold;">Adjuster Name &amp; Phone</label><input type="text" id="ag-adj"></div>
            <div><label style="font-size:12px;font-weight:bold;">Research Grant Account Code</label><input type="text" id="ag-grant"></div>
            <div style="grid-column:span 2;"><label style="font-size:12px;font-weight:bold;">Research Principal Investigator</label><input type="text" id="ag-pi"></div>
        </div>
        <button class="btn" style="background:#8e44ad;" onclick="submitOrderAccidentGrant()">Save Billing Category Details</button>
    </div>`;
}

async function submitOrderAccidentGrant() {
    const orderId = document.getElementById('ag-order').value;
    const cat = document.getElementById('ag-cat').value;
    const res = await execSQL(`
        INSERT OR REPLACE INTO order_billing_accident_grant_info
        (order_id, billing_category, date_of_injury_or_accident, wsib_or_mva_claim_number, employer_or_insurer_name, adjuster_name_and_phone, research_grant_account_code, research_principal_investigator)
        VALUES (${orderId}, '${cat}', ${sqlStr(document.getElementById('ag-doi').value, true)}, ${sqlStr(document.getElementById('ag-clm').value, true)},
                ${sqlStr(document.getElementById('ag-emp').value, true)}, ${sqlStr(document.getElementById('ag-adj').value, true)},
                ${sqlStr(document.getElementById('ag-grant').value, true)}, ${sqlStr(document.getElementById('ag-pi').value, true)})
    `);
    if (res && res.error) return setBillBanner(res.error, true);
    setBillBanner(`Saved ${cat} billing metadata for Order #${orderId}.`);
    switchBillTab('CLAIMS');
}

/* ============================================================================
 * TAB 3 & 4: FEE SCHEDULES, PAYERS & NCCI BUNDLING RULES
 * ============================================================================ */
async function renderFeesTab() {
    const [payers, rates] = await Promise.all([
        execSQL(`SELECT * FROM billing_payers ORDER BY payer_id`),
        execSQL(`SELECT f.*, p.payer_code FROM fee_schedule_rates f JOIN billing_payers p ON f.payer_id = p.payer_id ORDER BY f.billing_code, f.component_modifier`)
    ]);
    const payOpts = (Array.isArray(payers) ? payers : []).map(p => `<option value="${p.payer_id}">${escAttr(p.payer_code)} - ${escAttr(p.payer_name)}</option>`).join('');

    document.getElementById('bill-tab-body').innerHTML = `
    <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1); margin-bottom:14px;">
        <h4 style="margin:0 0 8px 0;">Billing Payers</h4>
        ${buildTable(payers)}
        <div style="display:grid; grid-template-columns:1fr 1.5fr 1.3fr 1fr 0.7fr auto; gap:6px; margin-top:8px; align-items:end;">
            <div><label style="font-size:11px;">Payer Code *</label><input type="text" id="bp-code" placeholder="OHIP / BLUECROSS"></div>
            <div><label style="font-size:11px;">Payer Name *</label><input type="text" id="bp-name"></div>
            <div><label style="font-size:11px;">Type</label>
                <select id="bp-type">
                    <option value="GOVERNMENT_PROVINCIAL">GOVERNMENT_PROVINCIAL</option>
                    <option value="COMMERCIAL_INSURANCE">COMMERCIAL_INSURANCE</option>
                    <option value="WORKERS_COMP_WSIB">WORKERS_COMP_WSIB</option>
                    <option value="AUTO_MVA">AUTO_MVA</option>
                    <option value="RESEARCH_GRANT">RESEARCH_GRANT</option>
                    <option value="SELF_PAY_PATIENT">SELF_PAY_PATIENT</option>
                </select>
            </div>
            <div><label style="font-size:11px;">EDI Receiver</label><input type="text" id="bp-edi"></div>
            <div><label style="font-size:11px;">Filing Days</label><input type="number" id="bp-days" value="90"></div>
            <button class="btn" style="margin:0;" onclick="addBillingPayer()">+ Add Payer</button>
        </div>
    </div>

    <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <h4 style="margin:0 0 8px 0;">Fee Schedule Rates</h4>
        ${buildTable(rates)}
        <div style="display:grid; grid-template-columns:1.2fr 0.8fr 0.7fr 1.5fr 0.8fr 0.8fr 0.6fr 0.9fr auto; gap:6px; margin-top:8px; align-items:end;">
            <div><label style="font-size:11px;">Payer</label><select id="fs-payer">${payOpts}</select></div>
            <div><label style="font-size:11px;">Code *</label><input type="text" id="fs-code" placeholder="X406 / 71260"></div>
            <div><label style="font-size:11px;">Mod</label>
                <select id="fs-mod"><option value="GLOBAL">GLOBAL</option><option value="TC">TC</option><option value="26">26</option><option value="NONE">NONE</option></select>
            </div>
            <div><label style="font-size:11px;">Description *</label><input type="text" id="fs-desc"></div>
            <div><label style="font-size:11px;">Gross $</label><input type="number" step="0.01" id="fs-gross" value="0"></div>
            <div><label style="font-size:11px;">Allowed $</label><input type="number" step="0.01" id="fs-allow" value="0"></div>
            <div><label style="font-size:11px;">wRVU</label><input type="number" step="0.01" id="fs-rvu" value="0"></div>
            <div><label style="font-size:11px;">Start Date</label><input type="date" id="fs-start" value="2026-01-01"></div>
            <button class="btn" style="margin:0;" onclick="addFeeRate()">+ Add Rate</button>
        </div>
    </div>`;
}

async function addBillingPayer() {
    const code = document.getElementById('bp-code').value.trim();
    const name = document.getElementById('bp-name').value.trim();
    if (!code || !name) return setBillBanner('Payer code and name are required.', true);
    const res = await execSQL(`
        INSERT INTO billing_payers (payer_code, payer_name, payer_type, edi_receiver_id, timely_filing_limit_days)
        VALUES (${sqlStr(code)}, ${sqlStr(name)}, '${document.getElementById('bp-type').value}', ${sqlStr(document.getElementById('bp-edi').value, true)}, ${Number(document.getElementById('bp-days').value || 90)})
    `);
    if (res && res.error) return setBillBanner(res.error, true);
    renderFeesTab();
}

async function addFeeRate() {
    const code = document.getElementById('fs-code').value.trim();
    const desc = document.getElementById('fs-desc').value.trim();
    if (!code || !desc) return setBillBanner('Billing code and description are required.', true);
    const res = await execSQL(`
        INSERT INTO fee_schedule_rates (payer_id, billing_code, component_modifier, description, gross_charge_amount, contractual_allowed_amount, work_rvu, effective_start_date)
        VALUES (${document.getElementById('fs-payer').value}, ${sqlStr(code)}, '${document.getElementById('fs-mod').value}', ${sqlStr(desc)},
                ${Number(document.getElementById('fs-gross').value || 0)}, ${Number(document.getElementById('fs-allow').value || 0)},
                ${Number(document.getElementById('fs-rvu').value || 0)}, '${document.getElementById('fs-start').value}')
    `);
    if (res && res.error) return setBillBanner(res.error, true);
    renderFeesTab();
}

async function renderBundlingTab() {
    const rules = await execSQL(`SELECT * FROM billing_bundling_rules ORDER BY rule_id DESC`);
    document.getElementById('bill-tab-body').innerHTML = `
    <div style="background:#fff; padding:14px; border-radius:4px; box-shadow:0 1px 3px rgba(0,0,0,0.1);">
        <h4 style="margin:0 0 8px 0;">NCCI / Provincial Bundling &amp; Modifier Rules</h4>
        ${buildTable(rules)}
        <div style="display:grid; grid-template-columns:0.8fr 0.8fr 1.3fr 0.9fr 1.5fr auto; gap:6px; margin-top:10px; align-items:end;">
            <div><label style="font-size:11px;">Primary Code A *</label><input type="text" id="br-a" placeholder="e.g. X406"></div>
            <div><label style="font-size:11px;">Secondary Code B *</label><input type="text" id="br-b" placeholder="e.g. X405"></div>
            <div><label style="font-size:11px;">Rule Action</label>
                <select id="br-act">
                    <option value="DENY_CODE_B_INCLUSIVE">DENY_CODE_B_INCLUSIVE</option>
                    <option value="REQUIRE_MODIFIER_59">REQUIRE_MODIFIER_59</option>
                    <option value="COMBINE_INTO_TARGET">COMBINE_INTO_TARGET</option>
                </select>
            </div>
            <div><label style="font-size:11px;">Target Code</label><input type="text" id="br-tgt"></div>
            <div><label style="font-size:11px;">Rule Notes</label><input type="text" id="br-notes"></div>
            <button class="btn" style="margin:0;" onclick="addBundlingRule()">+ Add Rule</button>
        </div>
    </div>`;
}

async function addBundlingRule() {
    const a = document.getElementById('br-a').value.trim();
    const b = document.getElementById('br-b').value.trim();
    if (!a || !b) return setBillBanner('Code A and Code B are required.', true);
    const res = await execSQL(`
        INSERT INTO billing_bundling_rules (code_a, code_b, rule_action, target_combined_code, rule_notes)
        VALUES (${sqlStr(a)}, ${sqlStr(b)}, '${document.getElementById('br-act').value}', ${sqlStr(document.getElementById('br-tgt').value, true)}, ${sqlStr(document.getElementById('br-notes').value, true)})
    `);
    if (res && res.error) return setBillBanner(res.error, true);
    renderBundlingTab();
}