# Enterprise RIS / PACS & Revenue Cycle Platform

A full-stack **Radiology Information System (RIS)**, **Picture Archiving and Communication System (PACS)**, and **Revenue Cycle Management (RCM)** web platform designed for diagnostic imaging clinics, hospital radiology departments, and provincial/commercial billing workflows.

---

## Key Features

### 1. Clinical & Diagnostic Imaging Workflow
* **Patient Demographics & Chart Security:** Full patient registration, MRN tracking, allergy/renal lab alerts (`eGFR`, `Creatinine`), and **HIPAA / PHIPA Break-Glass VIP Chart Protection** with mandatory clinical justification logging (`hipaa_phi_access_logs`).
* **Physician Reading Workspace:**
  * Integrated browser-based **DICOM Viewer** with configurable **Hanging Protocols** (grid layouts, Window/Level presets, and automatic side-by-side prior comparison studies).
  * **Structured Dropdown Reporting Engine**, voice/text macros, Common Data Elements (CDEs), and standardized scoring systems (**BI-RADS**, **LI-RADS**, **PI-RADS**, **TI-RADS**, **Lung-RADS**, **CAD-RADS**).
  * **Electronic Signature PIN Verification** (`FINAL`, `PRELIMINARY`, `ADDENDUM`, `CORRECTED`) with database-enforced report immutability triggers.
  * **ACR Critical & Unexpected Findings Escalation** with automated SLA tracking (`CAT1` 1-hr, `CAT2` 24-hr, `CAT3` 72-hr).
  * Case flagging for **Tumor Board**, **M&M QA**, and anonymized **ACR Teaching Files**.
* **Mammography Compliance (MQSA / OBSP):** Automated calculation of Abnormal Interpretation Rate (AIR), Cancer Detection Rate (CDR per 1,000), PPV1/PPV2/PPV3 outcomes audits, breast density tracking, and 7-day / 30-day patient lay summary letter SLAs.
* **Executive Clinical & QA Analytics:** Real-time turnaround time (TAT) breakdowns by modality, STAT order tracking, radiologist wRVU productivity, room utilization, and QC reject analysis.

### 2. Structured Reporting (DICOM SR / XML) & Typst PDF Engine
* **DICOM SR & XML Scraper (`structuredreport.js`):** Scrapes stored PACS DICOM objects, unlinked DICOM queues, or uploaded `.dcm` / `.xml` / `.sr` files into an interactive tag/section editor (`HISTORY`, `TECHNIQUE`, `FINDINGS`, `IMPRESSION`).
* **Live Typst Rendering:** Compiles structured report fields into formatted PDF documents in real time via **TypstJS** and server-side Typst endpoints (`/api/report_typ`, `/api/report_pdf`).
* **Secondary Capture DICOM Export:** Renders finalized structured reports as monochrome Secondary Capture DICOM images (`SOP Class 1.2.840.10008.5.1.4.1.1.7`) for local download or direct PACS ingestion.

### 3. Interoperability & Communications Queue
* **Multi-Channel Dispatch Engine (`comms.js`):** Asynchronous queue (`correspondence_dispatch_queue`) supporting **HL7 `ORU^R01`**, **HL7 `ORM^O01`**, **HL7 FHIR Push**, **Fax**, **Email**, **SMS**, **Phone TTS / Verbal Call Logs**, and **Patient Portal** notifications.
* **Role-Scoped Portals:**
  * **Referring Physician Portal:** View referred order statuses, finalized Typst PDF reports, and launch diagnostic study images.
  * **Patient Portal:** Self-service lookup by MRN & DOB for upcoming appointment prep instructions and downloadable finalized reports.

### 4. Revenue Cycle Management (RCM) & Billing
* **Automated Claim Generation & Pricing (`billing.js`):** Auto-prices imaging orders against payer-specific fee schedules with support for **Global**, **Split (`26` + `TC`)**, **Professional-Only (`26`)**, and **Technical-Only (`TC`)** billing components.
* **NCCI & Medical Necessity Claim Scrubber:** Validates rendering provider NPIs, signed final report prerequisites, ICD-10 diagnosis pointers, prior authorizations, and Column 1 / Column 2 NCCI bundling edits before batching.
* **Electronic Claims & Remittance:**
  * **X12 837P** batch file generator (`ISA` / `GS` / `ST` / `CLM` / `SV1`).
  * **OHIP EDT** (`HEB` / `HEH` / `HET` / `HEE`) provincial batch file generator.
  * **ERA 835 Auto-Posting:** Parses `TRN` / `CLP` / `CAS` segments to automatically post payer payments, `CO-45` contractual adjustments, `CO-59` MPPR reductions, and `PR-1/2/3` patient responsibility transfers.
* **Front-Desk POS & Special Case Classifications:** Point-of-sale receipting/voiding, General Ledger (GL) period close locking, and **WSIB / Motor Vehicle Accident (MVA) / Research Grant** metadata tracking.

---

## Repository Structure

```text
├── index.html               # Single-page application shell, role-scoped navigation & portals
├── patients.js              # Patient registration, demographics & VIP break-glass workflow
├── physician.js             # Reading workspace, hanging protocols, signing PIN, MQSA & QA analytics
├── structuredreport.js      # DICOM SR / XML scraper, Typst preview & Secondary Capture DICOM export
├── billing.js               # RCM claims, NCCI scrubber, EDI 837P, OHIP EDT, ERA 835 & POS receipts
├── comms.js                 # Multi-channel dispatch queue monitor, retry & channel remediation
└── admin.js                 # System settings, user management, Typst templates, HL7 & HIPAA audit logs
```

---

## Getting Started

### Prerequisites
* Backend HTTP/API server providing SQL execution (`execSQL`), DICOM storage/retrieval (`/download`, `/upload_new`), Typst rendering (`/api/report_typ`, `/api/report_pdf`), authentication/PIN verification (`/api/verify_signature_pin`, `/api/set_signature_pin`, `/api/break_glass`), and queue dispatch (`/api/dispatch_reminder`).
* SQLite (or compatible SQL database) initialized with the RIS/PACS schema (`patients`, `imaging_orders`, `dicom_studies`, `dicom_series`, `dicom_instances`, `radiology_reports`, `typst_report_templates`, `billing_claim_headers`, `correspondence_dispatch_queue`, `hipaa_phi_access_logs`, etc.).

### Installation & Running
1. Clone the repository into your web server's static root directory:
   ```bash
   git clone <repository-url>
   cd ris-pacs
   ```
2. Ensure all JavaScript modules (`patients.js`, `physician.js`, `structuredreport.js`, `billing.js`, `comms.js`, `admin.js`, and your DICOM/Typst viewer scripts) are referenced in `index.html` and served with `application/javascript` MIME types.
3. Start your backend server and navigate to `http://localhost:<port>/index.html`.

---

## Security & Compliance Notes
* **Audit Logging:** All demographic views, clinical order opens, PACS image launches, report views, and VIP chart break-glass overrides are recorded in `hipaa_phi_access_logs`.
* **Immutability Controls:** Signed final reports (`FINAL`, `ADDENDUM`, `CORRECTED`), voided POS receipts, and GL-closed billing claims are protected against destructive edits; amendments must be issued as versioned addenda or corrected reports.
