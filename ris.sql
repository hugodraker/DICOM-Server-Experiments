BEGIN TRANSACTION;
DROP TABLE IF EXISTS "app_settings";
CREATE TABLE "app_settings" (
	"setting_key"	TEXT,
	"setting_value"	TEXT NOT NULL,
	PRIMARY KEY("setting_key")
);
DROP TABLE IF EXISTS "appointment_slots";
CREATE TABLE "appointment_slots" (
	"slot_id"	INTEGER,
	"modality_id"	INTEGER NOT NULL,
	"order_id"	INTEGER UNIQUE,
	"slot_start_time"	TEXT NOT NULL,
	"slot_end_time"	TEXT NOT NULL,
	"slot_status"	TEXT NOT NULL DEFAULT 'BOOKED' CHECK("slot_status" IN ('AVAILABLE', 'BOOKED', 'BLOCKED_MAINTENANCE', 'RESERVED_STAT', 'COMPLETED', 'CANCELLED', 'NO_SHOW')),
	"booked_by_id"	INTEGER,
	"booked_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("slot_id" AUTOINCREMENT),
	FOREIGN KEY("booked_by_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id")
);
DROP TABLE IF EXISTS "billing_bundling_rules";
CREATE TABLE "billing_bundling_rules" (
	"rule_id"	INTEGER,
	"code_a"	TEXT NOT NULL,
	"code_b"	TEXT NOT NULL,
	"rule_action"	TEXT NOT NULL CHECK("rule_action" IN ('COMBINE_INTO_TARGET', 'DENY_CODE_B_INCLUSIVE', 'REQUIRE_MODIFIER_59')),
	"target_combined_code"	TEXT,
	"rule_notes"	TEXT,
	PRIMARY KEY("rule_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "billing_claim_headers";
CREATE TABLE "billing_claim_headers" (
	"claim_id"	INTEGER,
	"claim_control_number"	TEXT NOT NULL UNIQUE,
	"order_id"	INTEGER NOT NULL,
	"patient_id"	INTEGER NOT NULL,
	"facility_id"	INTEGER NOT NULL,
	"primary_payer_id"	INTEGER NOT NULL,
	"secondary_payer_id"	INTEGER,
	"active_payer_tier"	INTEGER DEFAULT 1 CHECK("active_payer_tier" IN (1, 2, 3, 0)),
	"rendering_radiologist_id"	INTEGER,
	"referring_md_id"	INTEGER NOT NULL,
	"prior_auth_number"	TEXT,
	"total_gross_charges"	NUMERIC(10, 2) DEFAULT 0.00,
	"total_contractual_expected"	NUMERIC(10, 2) DEFAULT 0.00,
	"total_payer_paid"	NUMERIC(10, 2) DEFAULT 0.00,
	"total_contractual_adjustments"	NUMERIC(10, 2) DEFAULT 0.00,
	"total_patient_paid"	NUMERIC(10, 2) DEFAULT 0.00,
	"patient_responsibility_balance"	NUMERIC(10, 2) DEFAULT 0.00,
	"remaining_total_balance"	NUMERIC(10, 2) DEFAULT 0.00,
	"claim_status"	TEXT NOT NULL DEFAULT 'DRAFT_SCRUBBING' CHECK("claim_status" IN ('DRAFT_SCRUBBING', 'HOLD_CODING_ERROR', 'READY_FOR_EDI_BATCH', 'SUBMITTED_PRIMARY', 'PARTIAL_PAID_CROSSOVER', 'DENIED_UNDER_APPEAL', 'PATIENT_STATEMENT_SENT', 'COLLECTIONS_AGENCY', 'PAID_CLOSED_ZERO_BAL')),
	"service_date"	TEXT NOT NULL,
	"submitted_at"	TEXT,
	"closed_at"	TEXT,
	"accounting_period_ym"	TEXT NOT NULL DEFAULT (strftime('%Y-%m', 'now')),
	"is_locked_gl_closed"	INTEGER DEFAULT 0,
	PRIMARY KEY("claim_id" AUTOINCREMENT),
	FOREIGN KEY("facility_id") REFERENCES "facilities"("facility_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("primary_payer_id") REFERENCES "billing_payers"("payer_id"),
	FOREIGN KEY("referring_md_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("rendering_radiologist_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("secondary_payer_id") REFERENCES "billing_payers"("payer_id")
);
DROP TABLE IF EXISTS "billing_claim_lines";
CREATE TABLE "billing_claim_lines" (
	"line_id"	INTEGER,
	"claim_id"	INTEGER NOT NULL,
	"line_number"	INTEGER NOT NULL,
	"charge_category"	TEXT NOT NULL CHECK("charge_category" IN ('PROCEDURE_TC', 'PROCEDURE_PC_26', 'PROCEDURE_GLOBAL', 'CONTRAST_HCPCS_JCODE', 'CONTRAST_WASTE_JW', 'AFTER_HOURS_SURCHARGE', 'IV_SEDATION_SUPPLY')),
	"billing_code"	TEXT NOT NULL,
	"modifier_1"	TEXT,
	"modifier_2"	TEXT,
	"icd10_pointer"	TEXT NOT NULL,
	"units_quantity"	NUMERIC(6, 2) NOT NULL DEFAULT 1.00,
	"unit_gross_charge"	NUMERIC(10, 2) NOT NULL,
	"line_gross_total"	NUMERIC(10, 2) DEFAULT 0.00,
	"line_expected_allowed"	NUMERIC(10, 2) NOT NULL,
	"work_rvu_earned"	NUMERIC(6, 2) DEFAULT 0.00,
	UNIQUE("claim_id","line_number"),
	PRIMARY KEY("line_id" AUTOINCREMENT),
	FOREIGN KEY("claim_id") REFERENCES "billing_claim_headers"("claim_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "billing_payers";
CREATE TABLE "billing_payers" (
	"payer_id"	INTEGER,
	"payer_code"	TEXT NOT NULL UNIQUE,
	"payer_name"	TEXT NOT NULL,
	"payer_type"	TEXT NOT NULL CHECK("payer_type" IN ('GOVERNMENT_PROVINCIAL', 'COMMERCIAL_INSURANCE', 'WORKERS_COMP_WSIB', 'AUTO_MVA', 'RESEARCH_GRANT', 'SELF_PAY_PATIENT')),
	"edi_receiver_id"	TEXT,
	"timely_filing_limit_days"	INTEGER DEFAULT 90,
	"is_active"	INTEGER DEFAULT 1,
	PRIMARY KEY("payer_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "billing_remittance_transactions";
CREATE TABLE "billing_remittance_transactions" (
	"tx_id"	INTEGER,
	"claim_id"	INTEGER NOT NULL,
	"line_id"	INTEGER,
	"tx_source"	TEXT NOT NULL CHECK("tx_source" IN ('PRIMARY_PAYER_ERA_835', 'SECONDARY_PAYER_EOB', 'PATIENT_POS_RECEIPT', 'ADMIN_WRITE_OFF')),
	"tx_type"	TEXT NOT NULL CHECK("tx_type" IN ('PAYMENT', 'CONTRACTUAL_ADJUSTMENT_CO45', 'MPPR_REDUCTION_ADJUSTMENT', 'DEDUCTIBLE_TRANSFER_PR1', 'COINSURANCE_TRANSFER_PR2', 'COPAY_TRANSFER_PR3', 'DENIAL_REVERSAL', 'REFUND_TO_PATIENT_OR_PAYER')),
	"amount"	NUMERIC(10, 2) NOT NULL,
	"carc_reason_code"	TEXT,
	"rarc_remark_code"	TEXT,
	"check_or_eft_trace_number"	TEXT,
	"posted_by_user_id"	INTEGER,
	"posted_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("tx_id" AUTOINCREMENT),
	FOREIGN KEY("claim_id") REFERENCES "billing_claim_headers"("claim_id"),
	FOREIGN KEY("line_id") REFERENCES "billing_claim_lines"("line_id"),
	FOREIGN KEY("posted_by_user_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "clinical_followup_recalls";
CREATE TABLE "clinical_followup_recalls" (
	"recall_id"	INTEGER,
	"source_report_id"	INTEGER NOT NULL,
	"patient_id"	INTEGER NOT NULL,
	"recommended_exam_code"	TEXT,
	"recall_reason"	TEXT NOT NULL,
	"due_date"	TEXT NOT NULL,
	"lay_letter_sent_to_patient_at"	TEXT,
	"followup_order_id"	INTEGER,
	"pathology_outcome"	TEXT CHECK("pathology_outcome" IN ('PENDING', 'BENIGN_CONCORDANT', 'MALIGNANT_CONCORDANT', 'DISCORDANT_NEEDS_REVIEW', 'NA')),
	"recall_status"	TEXT DEFAULT 'OPEN_DUE' CHECK("recall_status" IN ('OPEN_DUE', 'SCHEDULED', 'COMPLETED', 'PATIENT_DECLINED', 'LOST_TO_FOLLOWUP')),
	PRIMARY KEY("recall_id" AUTOINCREMENT),
	FOREIGN KEY("followup_order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("recommended_exam_code") REFERENCES "exam_catalog"("exam_code"),
	FOREIGN KEY("source_report_id") REFERENCES "radiology_reports"("report_id")
);
DROP TABLE IF EXISTS "contrast_and_medication_logs";
CREATE TABLE "contrast_and_medication_logs" (
	"admin_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"administered_by_id"	INTEGER NOT NULL,
	"agent_category"	TEXT NOT NULL CHECK("agent_category" IN ('IODINATED_CONTRAST', 'GADOLINIUM_CONTRAST', 'ORAL_BARIUM', 'SALINE_FLUSH', 'ANXIOLYTIC_SEDATION', 'ANTI_HISTAMINE_STEROID')),
	"agent_name"	TEXT NOT NULL,
	"lot_number"	TEXT NOT NULL,
	"expiration_date"	TEXT NOT NULL,
	"route"	TEXT NOT NULL CHECK("route" IN ('IV', 'ORAL', 'RECTAL', 'INTRA_ARTICULAR', 'INTRATHECAL', 'IM')),
	"iv_gauge_and_site"	TEXT,
	"volume_ml"	NUMERIC(6, 2) NOT NULL,
	"flow_rate_ml_sec"	NUMERIC(4, 1),
	"extravasation_occurred"	INTEGER DEFAULT 0,
	"adverse_reaction_occurred"	INTEGER DEFAULT 0,
	"reaction_notes_and_treatment"	TEXT,
	"administered_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("admin_id" AUTOINCREMENT),
	FOREIGN KEY("administered_by_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id")
);
DROP TABLE IF EXISTS "correspondence_dispatch_queue";
CREATE TABLE "correspondence_dispatch_queue" (
	"dispatch_id"	INTEGER,
	"order_id"	INTEGER,
	"report_id"	INTEGER,
	"patient_id"	INTEGER NOT NULL,
	"recipient_user_id"	INTEGER,
	"handled_by_user_id"	INTEGER,
	"recipient_type"	TEXT NOT NULL CHECK("recipient_type" IN ('PATIENT', 'REFERRING_MD', 'PRIMARY_CARE_MD', 'EXTERNAL_HOSPITAL', 'INTERNAL_STAFF')),
	"channel"	TEXT NOT NULL CHECK("channel" IN ('HL7_ORU_R01', 'HL7_ORM_O01', 'HL7_FHIR_PUSH', 'FAX', 'PATIENT_PORTAL', 'SMS', 'EMAIL', 'PHONE_TTS', 'PHONE_VERBAL')),
	"destination_endpoint"	TEXT NOT NULL,
	"message_type"	TEXT NOT NULL,
	"payload_content"	TEXT NOT NULL,
	"delivery_status"	TEXT NOT NULL DEFAULT 'QUEUED' CHECK("delivery_status" IN ('QUEUED', 'TRANSMITTING', 'SENT', 'DELIVERED_ACK', 'ACKNOWLEDGED', 'FAILED', 'FAILED_RETRYING', 'PERMANENT_FAILURE')),
	"retry_count"	INTEGER DEFAULT 0,
	"external_ack_id"	TEXT,
	"error_message"	TEXT,
	"queued_at"	TEXT DEFAULT (datetime('now')),
	"sent_at"	TEXT,
	"delivered_at"	TEXT,
	PRIMARY KEY("dispatch_id" AUTOINCREMENT),
	FOREIGN KEY("handled_by_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id") ON DELETE CASCADE,
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("recipient_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("report_id") REFERENCES "radiology_reports"("report_id")
);
DROP TABLE IF EXISTS "critical_findings_escalations";
CREATE TABLE "critical_findings_escalations" (
	"escalation_id"	INTEGER,
	"report_id"	INTEGER NOT NULL UNIQUE,
	"order_id"	INTEGER NOT NULL,
	"alert_tier"	TEXT NOT NULL CHECK("alert_tier" IN ('CAT1_STAT_1HR', 'CAT2_URGENT_24HR', 'CAT3_ACTIONABLE_72HR')),
	"sla_deadline_at"	TEXT NOT NULL,
	"initiated_by_rad_id"	INTEGER NOT NULL,
	"target_provider_id"	INTEGER NOT NULL,
	"escalation_status"	TEXT NOT NULL DEFAULT 'OPEN_PENDING_ACK' CHECK("escalation_status" IN ('OPEN_PENDING_ACK', 'ESCALATED_TO_BACKUP_MD', 'ACKNOWLEDGED_CLOSED')),
	"acknowledged_by_provider_id"	INTEGER,
	"acknowledged_by_name_and_role"	TEXT,
	"communication_method"	TEXT CHECK("communication_method" IN ('DIRECT_PHONE_CALL', 'IN_PERSON_VERBAL', 'SECURE_BIDIRECTIONAL_SMS_ACK')),
	"verbatim_readback_verified"	INTEGER DEFAULT 0 CHECK("verbatim_readback_verified" IN (0, 1)),
	"escalation_notes"	TEXT,
	"initiated_at"	TEXT DEFAULT (datetime('now')),
	"acknowledged_at"	TEXT,
	PRIMARY KEY("escalation_id" AUTOINCREMENT),
	FOREIGN KEY("acknowledged_by_provider_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("initiated_by_rad_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("report_id") REFERENCES "radiology_reports"("report_id"),
	FOREIGN KEY("target_provider_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "dicom_instances";
CREATE TABLE "dicom_instances" (
	"instance_id"	INTEGER,
	"series_id"	INTEGER NOT NULL,
	"study_id"	INTEGER NOT NULL,
	"sop_instance_uid"	TEXT NOT NULL UNIQUE,
	"instance_number"	INTEGER NOT NULL DEFAULT 1,
	"modality"	TEXT NOT NULL,
	"file_path"	TEXT NOT NULL,
	"file_size_bytes"	INTEGER DEFAULT 0,
	"rows"	INTEGER DEFAULT 256,
	"columns"	INTEGER DEFAULT 256,
	"bits_allocated"	INTEGER DEFAULT 8,
	"uploaded_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("instance_id" AUTOINCREMENT),
	FOREIGN KEY("series_id") REFERENCES "dicom_series"("series_id") ON DELETE CASCADE,
	FOREIGN KEY("study_id") REFERENCES "dicom_studies"("study_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "dicom_series";
CREATE TABLE "dicom_series" (
	"series_id"	INTEGER,
	"study_id"	INTEGER NOT NULL,
	"series_instance_uid"	TEXT NOT NULL UNIQUE,
	"series_number"	INTEGER NOT NULL,
	"modality"	TEXT NOT NULL,
	"protocol_name"	TEXT,
	"series_description"	TEXT,
	"body_part_examined"	TEXT,
	"instance_count"	INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY("series_id" AUTOINCREMENT),
	FOREIGN KEY("study_id") REFERENCES "dicom_studies"("study_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "dicom_studies";
CREATE TABLE "dicom_studies" (
	"study_id"	INTEGER,
	"order_id"	INTEGER NOT NULL UNIQUE,
	"study_instance_uid"	TEXT NOT NULL UNIQUE,
	"pacs_wado_url"	TEXT NOT NULL,
	"pacs_zero_footprint_viewer_url"	TEXT NOT NULL,
	"total_series_count"	INTEGER DEFAULT 0,
	"total_instance_count"	INTEGER DEFAULT 0,
	"kvp"	NUMERIC(5, 1),
	"mas"	NUMERIC(6, 1),
	"fluoroscopy_time_sec"	NUMERIC(6, 1),
	"ctdi_vol_mgy"	NUMERIC(8, 2),
	"dlp_mgy_cm"	NUMERIC(8, 2),
	"dap_gy_cm2"	NUMERIC(8, 2),
	"effective_dose_msv"	NUMERIC(6, 2),
	"dose_exceeded_diagnostic_ref_level"	INTEGER DEFAULT 0,
	"qa_verified_by_tech_id"	INTEGER,
	"tech_notes_for_radiologist"	TEXT,
	"stored_in_pacs_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("study_id" AUTOINCREMENT),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("qa_verified_by_tech_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "dicom_unlinked_queue";
CREATE TABLE "dicom_unlinked_queue" (
	"queue_id"	INTEGER,
	"sop_instance_uid"	TEXT NOT NULL UNIQUE,
	"study_instance_uid"	TEXT,
	"raw_accession"	TEXT,
	"raw_patient_id"	TEXT,
	"raw_patient_name"	TEXT,
	"modality"	TEXT,
	"file_path"	TEXT NOT NULL,
	"status"	TEXT DEFAULT 'UNLINKED',
	"received_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("queue_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "equipment_qa_logs";
CREATE TABLE "equipment_qa_logs" (
	"qa_id"	INTEGER,
	"modality_id"	INTEGER NOT NULL,
	"performed_by_id"	INTEGER NOT NULL,
	"qa_type"	TEXT NOT NULL CHECK("qa_type" IN ('DAILY_PHANTOM_CALIBRATION', 'WEEKLY_ARTIFACT_CHECK', 'ANNUAL_PHYSICIST_SURVEY', 'PREVENTIVE_MAINTENANCE')),
	"passed_calibration"	INTEGER NOT NULL CHECK("passed_calibration" IN (0, 1)),
	"measurements_json"	TEXT,
	"performed_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("qa_id" AUTOINCREMENT),
	FOREIGN KEY("modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("performed_by_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "exam_catalog";
CREATE TABLE "exam_catalog" (
	"exam_code"	TEXT,
	"exam_name"	TEXT NOT NULL,
	"modality_type"	TEXT NOT NULL CHECK("modality_type" IN ('XRAY', 'CT', 'MRI', 'US', 'MAMMO', 'FLUORO', 'NM', 'PET', 'DEXA')),
	"body_part"	TEXT NOT NULL,
	"laterality_required"	INTEGER DEFAULT 0,
	"default_duration_mins"	INTEGER NOT NULL DEFAULT 30,
	"requires_contrast"	INTEGER DEFAULT 0,
	"requires_renal_lab_check"	INTEGER DEFAULT 0,
	"requires_signed_consent"	INTEGER DEFAULT 0,
	"patient_prep_instructions"	TEXT,
	PRIMARY KEY("exam_code")
);
DROP TABLE IF EXISTS "exam_safety_screenings";
CREATE TABLE "exam_safety_screenings" (
	"screening_id"	INTEGER,
	"order_id"	INTEGER NOT NULL UNIQUE,
	"screened_by_id"	INTEGER NOT NULL,
	"verified_two_patient_identifiers"	INTEGER NOT NULL DEFAULT 0 CHECK("verified_two_patient_identifiers" IN (0, 1)),
	"verified_correct_exam_and_site"	INTEGER NOT NULL DEFAULT 0 CHECK("verified_correct_exam_and_site" IN (0, 1)),
	"pregnancy_status"	TEXT NOT NULL DEFAULT 'NOT_APPLICABLE' CHECK("pregnancy_status" IN ('NOT_APPLICABLE', 'NEGATIVE_BY_HISTORY', 'NEGATIVE_BY_LAB', 'POSITIVE', 'UNSURE')),
	"last_menstrual_period"	TEXT,
	"serum_creatinine_mg_dl"	NUMERIC(4, 2),
	"egfr_ml_min"	NUMERIC(5, 1),
	"renal_lab_drawn_date"	TEXT,
	"has_prior_contrast_reaction"	INTEGER DEFAULT 0,
	"premedication_required"	INTEGER DEFAULT 0,
	"premedication_verified_complete"	INTEGER DEFAULT 0,
	"has_cardiac_pacemaker_icd"	INTEGER DEFAULT 0,
	"has_aneurysm_clips_coils"	INTEGER DEFAULT 0,
	"has_cochlear_neurostimulator"	INTEGER DEFAULT 0,
	"has_metallic_orbital_foreign_body"	INTEGER DEFAULT 0,
	"mri_implant_safety_status"	TEXT DEFAULT 'NA' CHECK("mri_implant_safety_status" IN ('NA', 'MR_SAFE', 'MR_CONDITIONAL_CLEARED', 'MR_UNSAFE_CONTRAINDICATED')),
	"screening_clearance_status"	TEXT NOT NULL DEFAULT 'PENDING' CHECK("screening_clearance_status" IN ('PENDING', 'CLEARED', 'HELD_NEEDS_RAD_APPROVAL', 'CONTRAINDICATED_CANCELLED')),
	"radiologist_override_by_id"	INTEGER,
	"radiologist_override_notes"	TEXT,
	"screened_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("screening_id" AUTOINCREMENT),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("radiologist_override_by_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("screened_by_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "facilities";
CREATE TABLE "facilities" (
	"facility_id"	INTEGER,
	"facility_code"	TEXT NOT NULL UNIQUE,
	"facility_name"	TEXT NOT NULL,
	"address"	TEXT NOT NULL,
	"phone"	TEXT NOT NULL,
	"timezone"	TEXT DEFAULT 'America/New York',
	"is_active"	INTEGER DEFAULT 1,
	PRIMARY KEY("facility_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "fee_schedule_rates";
CREATE TABLE "fee_schedule_rates" (
	"rate_id"	INTEGER,
	"payer_id"	INTEGER NOT NULL,
	"billing_code"	TEXT NOT NULL,
	"component_modifier"	TEXT NOT NULL DEFAULT 'GLOBAL' CHECK("component_modifier" IN ('GLOBAL', 'TC', '26', 'NONE')),
	"description"	TEXT NOT NULL,
	"gross_charge_amount"	NUMERIC(10, 2) NOT NULL,
	"contractual_allowed_amount"	NUMERIC(10, 2) NOT NULL,
	"work_rvu"	NUMERIC(6, 2) DEFAULT 0.00,
	"technical_rvu"	NUMERIC(6, 2) DEFAULT 0.00,
	"mppr_family_group"	TEXT,
	"effective_start_date"	TEXT NOT NULL,
	"effective_end_date"	TEXT,
	UNIQUE("payer_id","billing_code","component_modifier","effective_start_date"),
	PRIMARY KEY("rate_id" AUTOINCREMENT),
	FOREIGN KEY("payer_id") REFERENCES "billing_payers"("payer_id")
);
DROP TABLE IF EXISTS "front_desk_pos_collections";
CREATE TABLE "front_desk_pos_collections" (
	"receipt_id"	INTEGER,
	"receipt_number"	TEXT NOT NULL UNIQUE,
	"patient_id"	INTEGER NOT NULL,
	"order_id"	INTEGER,
	"claim_id"	INTEGER,
	"collection_purpose"	TEXT NOT NULL CHECK("collection_purpose" IN ('COPAY', 'DEDUCTIBLE_ESTIMATE', 'SELF_PAY_PACKAGE', 'CD_FILM_COPY_FEE', 'PRIOR_BALANCE')),
	"payment_method"	TEXT NOT NULL CHECK("payment_method" IN ('CREDIT_CARD', 'DEBIT_INTERAC', 'CASH', 'CHEQUE', 'PATIENT_PORTAL_STRIPE')),
	"amount_collected"	NUMERIC(10, 2) NOT NULL,
	"terminal_auth_code"	TEXT,
	"collected_by_user_id"	INTEGER NOT NULL,
	"collected_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("receipt_id" AUTOINCREMENT),
	FOREIGN KEY("claim_id") REFERENCES "billing_claim_headers"("claim_id"),
	FOREIGN KEY("collected_by_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id")
);
DROP TABLE IF EXISTS "fts_radiology_reports";
CREATE VIRTUAL TABLE fts_radiology_reports USING fts5(
    accession_number,
    findings_section,
    impression_section,
    content='radiology_reports',
    content_rowid='report_id'
);
DROP TABLE IF EXISTS "fts_radiology_reports_config";
CREATE TABLE "fts_radiology_reports_config" (
	"k"	,
	"v"	,
	PRIMARY KEY("k")
) WITHOUT ROWID;
DROP TABLE IF EXISTS "fts_radiology_reports_data";
CREATE TABLE "fts_radiology_reports_data" (
	"id"	INTEGER,
	"block"	BLOB,
	PRIMARY KEY("id")
);
DROP TABLE IF EXISTS "fts_radiology_reports_docsize";
CREATE TABLE "fts_radiology_reports_docsize" (
	"id"	INTEGER,
	"sz"	BLOB,
	PRIMARY KEY("id")
);
DROP TABLE IF EXISTS "fts_radiology_reports_idx";
CREATE TABLE "fts_radiology_reports_idx" (
	"segid"	,
	"term"	,
	"pgno"	,
	PRIMARY KEY("segid","term")
) WITHOUT ROWID;
DROP TABLE IF EXISTS "hanging_protocols";
CREATE TABLE "hanging_protocols" (
	"hp_id"	INTEGER,
	"user_id"	INTEGER,
	"modality_type"	TEXT NOT NULL,
	"body_part"	TEXT,
	"layout_config_json"	TEXT NOT NULL,
	PRIMARY KEY("hp_id" AUTOINCREMENT),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "hipaa_phi_access_logs";
CREATE TABLE "hipaa_phi_access_logs" (
	"audit_id"	INTEGER,
	"user_id"	INTEGER NOT NULL,
	"patient_id"	INTEGER NOT NULL,
	"order_id"	INTEGER,
	"access_action"	TEXT NOT NULL CHECK("access_action" IN ('VIEW_DEMOGRAPHICS', 'VIEW_CLINICAL_ORDER', 'LAUNCH_PACS_IMAGES', 'VIEW_REPORT', 'PRINT_OR_EXPORT_PHI', 'BREAK_GLASS_VIP_OVERRIDE')),
	"workstation_ip"	TEXT NOT NULL,
	"access_reason"	TEXT,
	"accessed_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("audit_id" AUTOINCREMENT),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "hl7_inbound_staging";
CREATE TABLE "hl7_inbound_staging" (
	"hl7_id"	INTEGER,
	"message_control_id"	TEXT NOT NULL UNIQUE,
	"sending_application"	TEXT NOT NULL,
	"sending_facility"	TEXT NOT NULL,
	"message_type"	TEXT NOT NULL,
	"raw_hl7_payload"	TEXT NOT NULL,
	"processing_status"	TEXT DEFAULT 'RECEIVED' CHECK("processing_status" IN ('RECEIVED', 'PROCESSED', 'FAILED_QUARANTINE', 'IGNORED')),
	"error_diagnostics"	TEXT,
	"received_at"	TEXT DEFAULT (datetime('now')),
	"processed_at"	TEXT,
	PRIMARY KEY("hl7_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "icd10_diagnoses";
CREATE TABLE "icd10_diagnoses" (
	"icd10_code"	TEXT,
	"short_description"	TEXT NOT NULL,
	"full_description"	TEXT NOT NULL,
	PRIMARY KEY("icd10_code")
);
DROP TABLE IF EXISTS "imaging_orders";
CREATE TABLE "imaging_orders" (
	"order_id"	INTEGER,
	"accession_number"	TEXT NOT NULL UNIQUE,
	"facility_id"	INTEGER NOT NULL,
	"patient_id"	INTEGER NOT NULL,
	"referring_md_id"	INTEGER NOT NULL,
	"performing_tech_id"	INTEGER,
	"assisting_nurse_id"	INTEGER,
	"assigned_resident_id"	INTEGER,
	"assigned_radiologist_id"	INTEGER,
	"exam_code"	TEXT NOT NULL,
	"laterality"	TEXT DEFAULT 'NA' CHECK("laterality" IN ('NA', 'LEFT', 'RIGHT', 'BILATERAL')),
	"modality_id"	INTEGER,
	"clinical_indication"	TEXT NOT NULL,
	"relevant_clinical_history"	TEXT,
	"patient_class"	TEXT NOT NULL DEFAULT 'OUTPATIENT' CHECK("patient_class" IN ('OUTPATIENT', 'INPATIENT', 'EMERGENCY', 'URGENT_CARE')),
	"inpatient_ward_bed"	TEXT,
	"transport_mode"	TEXT DEFAULT 'AMBULATORY' CHECK("transport_mode" IN ('AMBULATORY', 'WHEELCHAIR', 'STRETCHER', 'PORTABLE_BEDSIDE')),
	"priority"	TEXT NOT NULL DEFAULT 'ROUTINE' CHECK("priority" IN ('ROUTINE', 'URGENT', 'STAT', 'CODE_STROKE_TRAUMA')),
	"queue_ticket_number"	TEXT,
	"insurance_id"	INTEGER,
	"prior_auth_number"	TEXT,
	"billing_status"	TEXT DEFAULT 'PENDING_AUTH' CHECK("billing_status" IN ('PENDING_AUTH', 'CLEARED', 'SELF_PAY', 'HOLD_DENIED', 'BILLED')),
	"workflow_status"	TEXT NOT NULL DEFAULT 'ORDERED' CHECK("workflow_status" IN ('ORDERED', 'PENDING_PRIOR_AUTH', 'SCHEDULED', 'IN_TRANSIT', 'ARRIVED_WAITING', 'IN_PREP_SCREENING', 'READY_FOR_SCAN', 'IN_PROGRESS', 'IMAGES_CAPTURED_QA', 'READY_FOR_READ', 'ASSIGNED_READING', 'PRELIMINARY_REPORTED', 'FINAL_REPORTED', 'RESULTS_DELIVERED', 'CLOSED', 'CANCELLED', 'NO_SHOW')),
	"cancellation_reason"	TEXT,
	"ordered_at"	TEXT DEFAULT (datetime('now')),
	"scheduled_at"	TEXT,
	"arrived_at"	TEXT,
	"prep_started_at"	TEXT,
	"scan_started_at"	TEXT,
	"scan_completed_at"	TEXT,
	"qa_completed_at"	TEXT,
	"reading_started_at"	TEXT,
	"final_reported_at"	TEXT,
	"closed_at"	TEXT,
	"preassigned_study_uid"	TEXT,
	"sps_id"	TEXT,
	"assigned_protocol_id"	INTEGER,
	"protocol_custom_notes"	TEXT,
	"mpps_status"	TEXT DEFAULT 'PENDING' CHECK("mpps_status" IN ('PENDING', 'IN_PROGRESS', 'COMPLETED', 'DISCONTINUED')),
	"mpps_discontinued_reason"	TEXT,
	PRIMARY KEY("order_id" AUTOINCREMENT),
	FOREIGN KEY("assigned_protocol_id") REFERENCES "scanner_protocols"("protocol_id"),
	FOREIGN KEY("assigned_radiologist_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("assigned_resident_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("assisting_nurse_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("exam_code") REFERENCES "exam_catalog"("exam_code"),
	FOREIGN KEY("facility_id") REFERENCES "facilities"("facility_id"),
	FOREIGN KEY("insurance_id") REFERENCES "patient_insurances"("insurance_id"),
	FOREIGN KEY("modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("performing_tech_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("referring_md_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "modalities_and_rooms";
CREATE TABLE "modalities_and_rooms" (
	"modality_id"	INTEGER,
	"facility_id"	INTEGER NOT NULL,
	"room_name"	TEXT NOT NULL,
	"modality_type"	TEXT NOT NULL CHECK("modality_type" IN ('XRAY', 'CT', 'MRI', 'US', 'MAMMO', 'FLUORO', 'NM', 'PET', 'DEXA')),
	"ae_title"	TEXT NOT NULL UNIQUE,
	"ip_address"	TEXT,
	"dicom_port"	INTEGER DEFAULT 104,
	"manufacturer_model"	TEXT,
	"serial_number"	TEXT,
	"operational_status"	TEXT DEFAULT 'ONLINE' CHECK("operational_status" IN ('ONLINE', 'MAINTENANCE', 'CALIBRATION_HOLD', 'OFFLINE')),
	"created_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("modality_id" AUTOINCREMENT),
	FOREIGN KEY("facility_id") REFERENCES "facilities"("facility_id")
);
DROP TABLE IF EXISTS "order_billing_accident_grant_info";
CREATE TABLE "order_billing_accident_grant_info" (
	"order_id"	INTEGER,
	"billing_category"	TEXT NOT NULL CHECK("billing_category" IN ('STANDARD_MEDICAL', 'WORKERS_COMP_WSIB', 'AUTO_ACCIDENT_MVA', 'RESEARCH_STUDY_GRANT', 'LEGAL_IME')),
	"date_of_injury_or_accident"	TEXT,
	"wsib_or_mva_claim_number"	TEXT,
	"employer_or_insurer_name"	TEXT,
	"adjuster_name_and_phone"	TEXT,
	"research_grant_account_code"	TEXT,
	"research_principal_investigator"	TEXT,
	PRIMARY KEY("order_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "order_diagnoses";
CREATE TABLE "order_diagnoses" (
	"order_id"	INTEGER NOT NULL,
	"icd10_code"	TEXT NOT NULL,
	"is_primary"	INTEGER DEFAULT 1,
	PRIMARY KEY("order_id","icd10_code"),
	FOREIGN KEY("icd10_code") REFERENCES "icd10_diagnoses"("icd10_code"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "order_dicom_study_map";
CREATE TABLE "order_dicom_study_map" (
	"order_id"	INTEGER NOT NULL,
	"study_id"	INTEGER NOT NULL,
	PRIMARY KEY("order_id","study_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id") ON DELETE CASCADE,
	FOREIGN KEY("study_id") REFERENCES "dicom_studies"("study_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "password_reset_tokens";
CREATE TABLE "password_reset_tokens" (
	"token_id"	INTEGER,
	"user_id"	INTEGER NOT NULL,
	"reset_token"	TEXT NOT NULL UNIQUE,
	"expires_at"	TEXT NOT NULL,
	"used_at"	TEXT,
	"created_at"	TEXT NOT NULL DEFAULT (datetime('now')),
	PRIMARY KEY("token_id" AUTOINCREMENT),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "pathology_specimen_chain";
CREATE TABLE "pathology_specimen_chain" (
	"specimen_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"container_barcode"	TEXT NOT NULL UNIQUE,
	"anatomical_source"	TEXT NOT NULL,
	"fixative_medium"	TEXT DEFAULT '10% Neutral Buffered Formalin',
	"collected_by_md_id"	INTEGER NOT NULL,
	"collected_at"	TEXT DEFAULT (datetime('now')),
	"courier_or_lab_receiver_name"	TEXT,
	"handed_off_to_pathology_at"	TEXT,
	"pathology_accession_number"	TEXT,
	PRIMARY KEY("specimen_id" AUTOINCREMENT),
	FOREIGN KEY("collected_by_md_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id")
);
DROP TABLE IF EXISTS "patient_allergies";
CREATE TABLE "patient_allergies" (
	"allergy_id"	INTEGER,
	"patient_id"	INTEGER NOT NULL,
	"allergen_category"	TEXT NOT NULL CHECK("allergen_category" IN ('IODINATED_CONTRAST', 'GADOLINIUM', 'LATEX', 'CHLORHEXIDINE', 'MEDICATION', 'OTHER')),
	"allergen_name"	TEXT NOT NULL,
	"reaction_description"	TEXT NOT NULL,
	"severity"	TEXT NOT NULL CHECK("severity" IN ('MILD', 'MODERATE', 'SEVERE_LIFE_THREATENING')),
	"recorded_by_id"	INTEGER,
	"recorded_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("allergy_id" AUTOINCREMENT),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("recorded_by_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "patient_consents";
CREATE TABLE "patient_consents" (
	"consent_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"consent_type"	TEXT NOT NULL CHECK("consent_type" IN ('IV_CONTRAST', 'MRI_SAFETY', 'PREGNANCY_RADIATION_WAIVER', 'INTERVENTIONAL_PROCEDURE', 'SEDATION')),
	"risks_explained_by_id"	INTEGER NOT NULL,
	"signed_by_name"	TEXT NOT NULL,
	"relationship_to_patient"	TEXT DEFAULT 'SELF',
	"signature_capture_ref"	TEXT,
	"signed_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("consent_id" AUTOINCREMENT),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("risks_explained_by_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "patient_insurances";
CREATE TABLE "patient_insurances" (
	"insurance_id"	INTEGER,
	"patient_id"	INTEGER NOT NULL,
	"coverage_rank"	INTEGER DEFAULT 1 CHECK("coverage_rank" IN (1, 2, 3)),
	"payer_name"	TEXT NOT NULL,
	"plan_name"	TEXT,
	"policy_number"	TEXT NOT NULL,
	"group_number"	TEXT,
	"subscriber_name"	TEXT,
	"subscriber_relationship"	TEXT DEFAULT 'SELF',
	"is_active"	INTEGER DEFAULT 1,
	PRIMARY KEY("insurance_id" AUTOINCREMENT),
	FOREIGN KEY("patient_id") REFERENCES "patients"("patient_id")
);
DROP TABLE IF EXISTS "patient_merge_history";
CREATE TABLE "patient_merge_history" (
	"merge_id"	INTEGER,
	"surviving_patient_id"	INTEGER NOT NULL,
	"retired_patient_id"	INTEGER NOT NULL,
	"hl7_control_id"	TEXT,
	"merged_by_user_id"	INTEGER,
	"pacs_dicom_merge_queued"	INTEGER DEFAULT 1,
	"merged_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("merge_id" AUTOINCREMENT),
	FOREIGN KEY("hl7_control_id") REFERENCES "hl7_inbound_staging"("message_control_id"),
	FOREIGN KEY("merged_by_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("retired_patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("surviving_patient_id") REFERENCES "patients"("patient_id")
);
DROP TABLE IF EXISTS "patients";
CREATE TABLE "patients" (
	"patient_id"	INTEGER,
	"mrn"	TEXT NOT NULL UNIQUE,
	"enterprise_mpi_id"	TEXT,
	"health_card_or_ssn"	TEXT UNIQUE,
	"first_name"	TEXT NOT NULL,
	"middle_name"	TEXT,
	"last_name"	TEXT NOT NULL,
	"preferred_name"	TEXT,
	"dob"	TEXT NOT NULL,
	"biological_sex"	TEXT NOT NULL CHECK("biological_sex" IN ('M', 'F', 'U', 'O')),
	"gender_identity"	TEXT,
	"weight_kg"	NUMERIC(5, 2),
	"height_cm"	NUMERIC(5, 2),
	"phone_primary"	TEXT NOT NULL,
	"phone_secondary"	TEXT,
	"email"	TEXT,
	"preferred_comm_channel"	TEXT DEFAULT 'PHONE',
	"address"	TEXT,
	"address_line1"	TEXT,
	"address_line2"	TEXT,
	"city"	TEXT,
	"state_province"	TEXT,
	"postal_code"	TEXT,
	"country"	TEXT DEFAULT 'CA',
	"preferred_language"	TEXT DEFAULT 'en',
	"emergency_contact_name"	TEXT,
	"emergency_contact_phone"	TEXT,
	"emergency_contact_relation"	TEXT,
	"patient_portal_enabled"	INTEGER DEFAULT 1,
	"is_VIP_restricted"	INTEGER DEFAULT 0,
	"insurance_provider"	TEXT,
	"insurance_policy_number"	TEXT,
	"insurance_group_number"	TEXT,
	"default_payer_id"	INTEGER,
	"primary_care_md_id"	INTEGER,
	"allergies"	TEXT,
	"contrast_allergy_flag"	INTEGER DEFAULT 0,
	"contrast_allergy_details"	TEXT,
	"mri_implant_flag"	INTEGER DEFAULT 0,
	"mri_safety_notes"	TEXT,
	"pregnancy_status"	TEXT DEFAULT 'UNKNOWN',
	"egfr_value"	REAL,
	"creatinine_value"	REAL,
	"renal_lab_date"	TEXT,
	"claustrophobia_flag"	INTEGER DEFAULT 0,
	"fall_risk_flag"	INTEGER DEFAULT 0,
	"isolation_precautions"	TEXT,
	"remind_email"	INTEGER DEFAULT 0,
	"remind_sms"	INTEGER DEFAULT 0,
	"remind_phone"	INTEGER DEFAULT 0,
	"remind_phone_mode"	TEXT DEFAULT 'TTS' CHECK("remind_phone_mode" IN ('TTS', 'VERBAL')),
	"patient_status"	TEXT DEFAULT 'ACTIVE',
	"deceased_date"	TEXT,
	"is_merged"	INTEGER DEFAULT 0,
	"merged_into_patient_id"	INTEGER,
	"is_active"	INTEGER DEFAULT 1,
	"notes"	TEXT,
	"created_at"	TEXT DEFAULT (datetime('now')),
	"updated_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("patient_id" AUTOINCREMENT),
	FOREIGN KEY("merged_into_patient_id") REFERENCES "patients"("patient_id"),
	FOREIGN KEY("primary_care_md_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "probe_disinfection_logs";
CREATE TABLE "probe_disinfection_logs" (
	"hld_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"modality_id"	INTEGER NOT NULL,
	"probe_serial_number"	TEXT NOT NULL,
	"disinfection_unit_id"	TEXT NOT NULL,
	"cycle_batch_number"	TEXT NOT NULL,
	"chemical_indicator_passed"	INTEGER NOT NULL CHECK("chemical_indicator_passed" IN (0, 1)),
	"performed_by_tech_id"	INTEGER NOT NULL,
	"disinfected_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("hld_id" AUTOINCREMENT),
	FOREIGN KEY("modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("performed_by_tech_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "procedure_vital_signs";
CREATE TABLE "procedure_vital_signs" (
	"vital_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"recorded_by_nurse_id"	INTEGER NOT NULL,
	"systolic_bp"	INTEGER NOT NULL,
	"diastolic_bp"	INTEGER NOT NULL,
	"heart_rate_bpm"	INTEGER NOT NULL,
	"spo2_percent"	INTEGER NOT NULL,
	"respiratory_rate"	INTEGER,
	"aldrete_sedation_score"	INTEGER CHECK("aldrete_sedation_score" BETWEEN 0 AND 10),
	"recorded_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("vital_id" AUTOINCREMENT),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("recorded_by_nurse_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "radiologist_voice_macros";
CREATE TABLE "radiologist_voice_macros" (
	"macro_id"	INTEGER,
	"user_id"	INTEGER,
	"trigger_phrase"	TEXT NOT NULL,
	"modality_type"	TEXT,
	"expansion_text"	TEXT NOT NULL,
	PRIMARY KEY("macro_id" AUTOINCREMENT),
	UNIQUE("user_id","trigger_phrase"),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "radiology_reports";
CREATE TABLE "radiology_reports" (
	"report_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"version_number"	INTEGER NOT NULL DEFAULT 1,
	"parent_report_id"	INTEGER,
	"drafted_by_resident_id"	INTEGER,
	"signing_radiologist_id"	INTEGER NOT NULL,
	"report_status"	TEXT NOT NULL DEFAULT 'DRAFT' CHECK("report_status" IN ('DRAFT', 'PRELIMINARY', 'FINAL', 'ADDENDUM', 'CORRECTED')),
	"clinical_history_section"	TEXT,
	"comparison_studies_section"	TEXT,
	"technique_section"	TEXT NOT NULL,
	"findings_section"	TEXT NOT NULL,
	"impression_section"	TEXT NOT NULL,
	"standardized_category_system"	TEXT DEFAULT 'NONE' CHECK("standardized_category_system" IN ('NONE', 'BI-RADS', 'LI-RADS', 'PI-RADS', 'TI-RADS', 'Lung-RADS', 'CAD-RADS')),
	"standardized_category_score"	TEXT,
	"is_critical_or_unexpected"	INTEGER DEFAULT 0 CHECK("is_critical_or_unexpected" IN (0, 1)),
	"acr_actionable_category"	TEXT DEFAULT 'NONE' CHECK("acr_actionable_category" IN ('NONE', 'CAT1_IMMEDIATE_LIFE_THREAT_1HR', 'CAT2_URGENT_WITHIN_24HR', 'CAT3_INCIDENTAL_MALIGNANCY_72HR')),
	"radpeer_score"	TEXT CHECK("radpeer_score" IN ('1_CONCUR', '2A_MINOR_DISCREPANCY', '2B_MAJOR_DISCREPANCY')),
	"peer_reviewed_by_id"	INTEGER,
	"dictated_at"	TEXT DEFAULT (datetime('now')),
	"preliminary_released_at"	TEXT,
	"final_signed_at"	TEXT,
	PRIMARY KEY("report_id" AUTOINCREMENT),
	FOREIGN KEY("drafted_by_resident_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("parent_report_id") REFERENCES "radiology_reports"("report_id"),
	FOREIGN KEY("peer_reviewed_by_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("signing_radiologist_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "radiopharmaceutical_hotlab_logs";
CREATE TABLE "radiopharmaceutical_hotlab_logs" (
	"hotlab_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"isotope_and_agent"	TEXT NOT NULL,
	"vial_lot_number"	TEXT NOT NULL,
	"calibrated_activity_mbq"	NUMERIC(8, 2) NOT NULL,
	"calibration_timestamp"	TEXT NOT NULL,
	"pre_injection_syringe_mbq"	NUMERIC(8, 2) NOT NULL,
	"measured_at"	TEXT NOT NULL,
	"post_injection_residual_mbq"	NUMERIC(8, 2) NOT NULL,
	"net_administered_mbq"	NUMERIC(8, 2) DEFAULT 0.00,
	"administered_by_tech_id"	INTEGER NOT NULL,
	"injected_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("hotlab_id" AUTOINCREMENT),
	FOREIGN KEY("administered_by_tech_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id")
);
DROP TABLE IF EXISTS "radreport_cde_definitions";
CREATE TABLE "radreport_cde_definitions" (
	"cde_id"	TEXT,
	"template_id"	INTEGER,
	"prompt_label"	TEXT NOT NULL,
	"data_type"	TEXT NOT NULL CHECK("data_type" IN ('SINGLE_SELECT', 'MULTI_SELECT', 'NUMERIC_MM', 'BOOLEAN', 'FREE_TEXT')),
	"allowed_values_json"	TEXT,
	"branching_condition_json"	TEXT,
	PRIMARY KEY("cde_id"),
	FOREIGN KEY("template_id") REFERENCES "report_templates"("template_id")
);
DROP TABLE IF EXISTS "rejected_image_logs";
CREATE TABLE "rejected_image_logs" (
	"reject_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"modality_id"	INTEGER NOT NULL,
	"tech_id"	INTEGER NOT NULL,
	"rejected_sop_instance_uid"	TEXT NOT NULL,
	"reject_reason"	TEXT NOT NULL CHECK("reject_reason" IN ('POSITIONING_ERROR', 'PATIENT_MOTION', 'EXPOSURE_UNDER_OVER', 'GRID_OR_DETECTOR_ARTIFACT', 'WRONG_MARKER_LATERALITY', 'EQUIPMENT_TEST')),
	"rejected_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("reject_id" AUTOINCREMENT),
	FOREIGN KEY("modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id"),
	FOREIGN KEY("tech_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "report_discrete_findings";
CREATE TABLE "report_discrete_findings" (
	"report_id"	INTEGER NOT NULL,
	"cde_id"	TEXT NOT NULL,
	"selected_value"	TEXT NOT NULL,
	"numeric_score"	INTEGER DEFAULT 0,
	PRIMARY KEY("report_id","cde_id"),
	FOREIGN KEY("cde_id") REFERENCES "radreport_cde_definitions"("cde_id"),
	FOREIGN KEY("report_id") REFERENCES "radiology_reports"("report_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "report_templates";
CREATE TABLE "report_templates" (
	"template_id"	INTEGER,
	"exam_code"	TEXT,
	"template_name"	TEXT NOT NULL,
	"technique_boilerplate"	TEXT NOT NULL,
	"structured_findings_template"	TEXT NOT NULL,
	"normal_impression_boilerplate"	TEXT NOT NULL,
	PRIMARY KEY("template_id" AUTOINCREMENT),
	FOREIGN KEY("exam_code") REFERENCES "exam_catalog"("exam_code")
);
DROP TABLE IF EXISTS "report_typst_documents";
CREATE TABLE "report_typst_documents" (
	"typst_doc_id"	INTEGER,
	"report_id"	INTEGER NOT NULL UNIQUE,
	"order_id"	INTEGER NOT NULL,
	"typst_source"	TEXT NOT NULL,
	"template_name"	TEXT NOT NULL DEFAULT 'STANDARD_RADIOLOGY_REPORT',
	"generated_at"	TEXT NOT NULL DEFAULT (datetime('now')),
	"updated_at"	TEXT NOT NULL DEFAULT (datetime('now')),
	PRIMARY KEY("typst_doc_id" AUTOINCREMENT),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id") ON DELETE CASCADE,
	FOREIGN KEY("report_id") REFERENCES "radiology_reports"("report_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "roles";
CREATE TABLE "roles" (
	"role_id"	INTEGER,
	"role_code"	TEXT NOT NULL CHECK("role_code" IN ('SYSTEM_ADMIN', 'PACS_ADMIN', 'RECEPTIONIST', 'SCHEDULER', 'BILLING_SPECIALIST', 'TRANSPORT', 'NURSE', 'TECHNOLOGIST', 'RESIDENT_FELLOW', 'RADIOLOGIST', 'REFERRING_MD', 'SAFETY_OFFICER')) UNIQUE,
	"role_description"	TEXT NOT NULL,
	"can_order_exams"	INTEGER DEFAULT 0,
	"can_perform_exams"	INTEGER DEFAULT 0,
	"can_draft_reports"	INTEGER DEFAULT 0,
	"can_sign_final_reports"	INTEGER DEFAULT 0,
	"can_override_safety"	INTEGER DEFAULT 0,
	PRIMARY KEY("role_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "rsna_reporting_templates";
CREATE TABLE "rsna_reporting_templates" (
	"template_row_id"	INTEGER,
	"modality"	TEXT NOT NULL,
	"anatomy"	TEXT NOT NULL,
	"pathology"	TEXT NOT NULL,
	"v1_label"	TEXT DEFAULT '',
	"v1_key"	TEXT DEFAULT '',
	"v1_values"	TEXT DEFAULT '',
	"v2_label"	TEXT DEFAULT '',
	"v2_key"	TEXT DEFAULT '',
	"v2_values"	TEXT DEFAULT '',
	"v3_label"	TEXT DEFAULT '',
	"v3_key"	TEXT DEFAULT '',
	"v3_values"	TEXT DEFAULT '',
	"v4_label"	TEXT DEFAULT '',
	"v4_key"	TEXT DEFAULT '',
	"v4_values"	TEXT DEFAULT '',
	"base_template"	TEXT NOT NULL,
	"findings_template"	TEXT NOT NULL,
	"impression_template"	TEXT NOT NULL,
	PRIMARY KEY("template_row_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "scanner_protocols";
CREATE TABLE "scanner_protocols" (
	"protocol_id"	INTEGER,
	"modality_type"	TEXT NOT NULL,
	"protocol_code"	TEXT NOT NULL UNIQUE,
	"protocol_name"	TEXT NOT NULL,
	"contrast_agent_default"	TEXT,
	"contrast_volume_ml_default"	NUMERIC(5, 1),
	"injection_rate_ml_sec"	NUMERIC(4, 1),
	"scan_delay_instructions"	TEXT,
	"reconstruction_instructions"	TEXT,
	PRIMARY KEY("protocol_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "staff_schedule_profiles";
CREATE TABLE "staff_schedule_profiles" (
	"user_id"	INTEGER,
	"is_schedulable"	INTEGER NOT NULL DEFAULT 1,
	"default_facility_id"	INTEGER,
	"default_modality_id"	INTEGER,
	"coverage_hours"	TEXT DEFAULT '',
	"preferred_hours"	TEXT DEFAULT '',
	"allowed_days"	TEXT DEFAULT '',
	"rest_hours"	REAL,
	"max_daily_hours"	REAL,
	"max_weekly_hours"	REAL,
	PRIMARY KEY("user_id"),
	FOREIGN KEY("default_facility_id") REFERENCES "facilities"("facility_id"),
	FOREIGN KEY("default_modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "staff_shift_schedules";
CREATE TABLE "staff_shift_schedules" (
	"shift_id"	INTEGER,
	"user_id"	INTEGER NOT NULL,
	"facility_id"	INTEGER NOT NULL,
	"assigned_modality_id"	INTEGER,
	"shift_type"	TEXT NOT NULL CHECK("shift_type" IN ('WORKING_REGULAR', 'ON_CALL_PRIMARY', 'ON_CALL_BACKUP', 'OFF_SICK', 'OFF_VACATION', 'OFF_CME_TRAINING')),
	"shift_start"	TEXT NOT NULL,
	"shift_end"	TEXT NOT NULL,
	"duration_hours"	NUMERIC(4, 2) DEFAULT 0.00,
	"notes"	TEXT,
	PRIMARY KEY("shift_id" AUTOINCREMENT),
	FOREIGN KEY("assigned_modality_id") REFERENCES "modalities_and_rooms"("modality_id"),
	FOREIGN KEY("facility_id") REFERENCES "facilities"("facility_id"),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id")
);
DROP TABLE IF EXISTS "staff_users";
CREATE TABLE "staff_users" (
	"user_id"	INTEGER,
	"username"	TEXT NOT NULL UNIQUE,
	"password_hash"	TEXT NOT NULL,
	"role_id"	INTEGER NOT NULL,
	"primary_facility_id"	INTEGER,
	"npi_or_license"	TEXT UNIQUE,
	"first_name"	TEXT NOT NULL,
	"last_name"	TEXT NOT NULL,
	"credentials"	TEXT,
	"specialty"	TEXT,
	"work_phone"	TEXT,
	"mobile_or_pager"	TEXT,
	"email"	TEXT NOT NULL UNIQUE,
	"preferred_comm_channel"	TEXT DEFAULT 'HL7_EHR' CHECK("preferred_comm_channel" IN ('HL7_EHR', 'FAX', 'PORTAL', 'EMAIL', 'SMS')),
	"fax_number"	TEXT,
	"digital_signature_pin_hash"	TEXT,
	"is_active"	INTEGER DEFAULT 1,
	"created_at"	TEXT DEFAULT (datetime('now')),
	PRIMARY KEY("user_id" AUTOINCREMENT),
	FOREIGN KEY("primary_facility_id") REFERENCES "facilities"("facility_id"),
	FOREIGN KEY("role_id") REFERENCES "roles"("role_id")
);
DROP TABLE IF EXISTS "tumor_board_and_teaching_cases";
CREATE TABLE "tumor_board_and_teaching_cases" (
	"collection_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"added_by_user_id"	INTEGER NOT NULL,
	"collection_type"	TEXT NOT NULL CHECK("collection_type" IN ('TUMOR_BOARD', 'MORBIDITY_MORTALITY_QA', 'TEACHING_FILE_ACR')),
	"conference_name_or_topic"	TEXT NOT NULL,
	"conference_date"	TEXT,
	"clinical_question_or_teaching_point"	TEXT NOT NULL,
	"is_anonymized_for_teaching"	INTEGER DEFAULT 0,
	PRIMARY KEY("collection_id" AUTOINCREMENT),
	FOREIGN KEY("added_by_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id")
);
DROP TABLE IF EXISTS "typst_report_templates";
CREATE TABLE "typst_report_templates" (
	"typst_template_id"	INTEGER,
	"template_code"	TEXT NOT NULL UNIQUE,
	"template_name"	TEXT NOT NULL,
	"modality_type"	TEXT NOT NULL DEFAULT 'ALL',
	"is_default"	INTEGER NOT NULL DEFAULT 0,
	"typst_markup"	TEXT NOT NULL,
	"updated_at"	TEXT NOT NULL DEFAULT (datetime('now')),
	PRIMARY KEY("typst_template_id" AUTOINCREMENT)
);
DROP TABLE IF EXISTS "user_auth_credentials";
CREATE TABLE "user_auth_credentials" (
	"user_id"	INTEGER,
	"password_salt"	TEXT NOT NULL,
	"password_hash"	TEXT NOT NULL,
	"must_change_password"	INTEGER NOT NULL DEFAULT 0,
	"last_login_at"	TEXT,
	"updated_at"	TEXT NOT NULL DEFAULT (datetime('now')),
	PRIMARY KEY("user_id"),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "user_sessions";
CREATE TABLE "user_sessions" (
	"session_token"	TEXT,
	"user_id"	INTEGER NOT NULL,
	"created_at"	TEXT NOT NULL DEFAULT (datetime('now')),
	"expires_at"	TEXT NOT NULL,
	PRIMARY KEY("session_token"),
	FOREIGN KEY("user_id") REFERENCES "staff_users"("user_id") ON DELETE CASCADE
);
DROP TABLE IF EXISTS "workflow_state_history";
CREATE TABLE "workflow_state_history" (
	"history_id"	INTEGER,
	"order_id"	INTEGER NOT NULL,
	"previous_status"	TEXT,
	"new_status"	TEXT NOT NULL,
	"changed_by_user_id"	INTEGER,
	"transition_timestamp"	TEXT DEFAULT (datetime('now')),
	"transition_notes"	TEXT,
	PRIMARY KEY("history_id" AUTOINCREMENT),
	FOREIGN KEY("changed_by_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id")
);
DROP TABLE IF EXISTS "worklist_locks";
CREATE TABLE "worklist_locks" (
	"order_id"	INTEGER,
	"locked_by_user_id"	INTEGER NOT NULL,
	"workstation_ip"	TEXT NOT NULL,
	"locked_at"	TEXT DEFAULT (datetime('now')),
	"heartbeat_expires_at"	TEXT NOT NULL,
	PRIMARY KEY("order_id"),
	FOREIGN KEY("locked_by_user_id") REFERENCES "staff_users"("user_id"),
	FOREIGN KEY("order_id") REFERENCES "imaging_orders"("order_id") ON DELETE CASCADE
);
INSERT INTO "fts_radiology_reports_config" ("k","v") VALUES ('version',4);
INSERT INTO "fts_radiology_reports_data" ("id","block") VALUES (1,X''),
 (10,X'00000000000000');
INSERT INTO "roles" ("role_id","role_code","role_description","can_order_exams","can_perform_exams","can_draft_reports","can_sign_final_reports","can_override_safety") VALUES (1,'SYSTEM_ADMIN','System Administrator',1,1,1,1,1);
INSERT INTO "staff_users" ("user_id","username","password_hash","role_id","primary_facility_id","npi_or_license","first_name","last_name","credentials","specialty","work_phone","mobile_or_pager","email","preferred_comm_channel","fax_number","digital_signature_pin_hash","is_active","created_at") VALUES (1,'admin','07f837aea9f3d6eed56b1186089e223bcfb225094f701594423217858cbcc737',1,1,NULL,'System','Admin',NULL,NULL,NULL,NULL,'admin@localhost','PORTAL',NULL,NULL,1,'2026-11-21 05:31:35');
INSERT INTO "typst_report_templates" ("typst_template_id","template_code","template_name","modality_type","is_default","typst_markup","updated_at") VALUES (1,'STANDARD_RADIOLOGY_REPORT','Standard Radiology Report (US Letter)','ALL',1,'#set page(paper: "us-letter", margin: (x: 0.75in, y: 0.85in),
  header: [#text(size: 9pt, fill: rgb(74, 85, 104))[{facility_name} - {patient_name} - Accession {accession}]],
  footer: [#text(size: 8pt, fill: rgb(113, 128, 150))[CONFIDENTIAL MEDICAL RECORD - {facility_name} - Page #counter(page).display()]]
)
#set text(font: "Helvetica", size: 10pt)

#text(size: 14pt, weight: "bold")[{facility_name}]
#text(size: 9pt, fill: rgb(74, 85, 104))[{facility_address} | Tel: {facility_phone}]
#line(length: 100%)

*Patient:* {patient_name}   |   *Accession:* {accession}
*MRN:* {mrn}   |   *DOB:* {dob} ({sex})
*Exam:* {exam_name} ({modality})
*Referring MD:* Dr. {referring_md}   |   *Status:* {report_status} (v{version}) - {signed_at}
#line(length: 100%)

== CLINICAL HISTORY
{clinical_history}

== COMPARISON
{comparison}

== TECHNIQUE
{technique}

== FINDINGS
{findings}

== IMPRESSION
*{impression}*

{standardized_assessment}
#line(length: 100%)
*Electronically Signed By:* Dr. {radiologist} (License/NPI: {radiologist_license}) on {signed_at}
','2026-11-21 05:19:11');
INSERT INTO "user_auth_credentials" ("user_id","password_salt","password_hash","must_change_password","last_login_at","updated_at") VALUES (1,'e387e25e6d86aeacfdff66645eeff079','07f837aea9f3d6eed56b1186089e223bcfb225094f701594423217858cbcc737',0,'2026-11-21 05:31:59','2026-11-21 05:31:35');
INSERT INTO "user_sessions" ("session_token","user_id","created_at","expires_at") VALUES ('2e16888941a338acf6993cf3379f5c8610752bd3e1347b12',1,'2026-11-21 05:31:59','2026-11-21 17:31:59');
DROP VIEW IF EXISTS "vw_dicom_modality_worklist";
CREATE VIEW vw_dicom_modality_worklist AS
SELECT 
    'ISO_IR 192' AS specific_character_set_0008_0005,
    o.accession_number AS accession_number_0008_0050,
    ec.modality_type AS modality_0008_0060,
    COALESCE(ref.last_name || '^' || ref.first_name, 'UNKNOWN') AS referring_physician_0008_0090,
    ec.exam_name AS study_description_0008_1030,
    UPPER(p.last_name || '^' || p.first_name || COALESCE('^' || p.middle_name, '')) AS patient_name_0010_0010,
    p.mrn AS patient_id_0010_0020,
    REPLACE(substr(p.dob, 1, 10), '-', '') AS patient_birth_date_0010_0030,
    p.biological_sex AS patient_sex_0010_0040,
    p.weight_kg AS patient_weight_0010_1030,
    COALESCE(o.preassigned_study_uid, '1.2.826.0.1.3680043.8.498.' || o.order_id) AS study_instance_uid_0020_000d,
    o.clinical_indication AS reason_for_requested_procedure_0040_1002,
    CASE o.priority
        WHEN 'CODE_STROKE_TRAUMA' THEN 'STAT'
        WHEN 'STAT' THEN 'STAT'
        WHEN 'URGENT' THEN 'HIGH'
        ELSE 'ROUTINE'
    END AS requested_procedure_priority_0040_1003,
    COALESCE(m.ae_title, 'ANY_SCP') AS scheduled_station_ae_title_0040_0001,
    -- Fallback to ordered_at if scheduled_at is NULL (e.g. walk-in ER STAT)
    REPLACE(substr(COALESCE(o.scheduled_at, o.ordered_at), 1, 10), '-', '') AS sps_start_date_0040_0002,
    REPLACE(substr(COALESCE(o.scheduled_at, o.ordered_at), 12, 8), ':', '') AS sps_start_time_0040_0003,
    COALESCE(o.sps_id, 'SPS-' || o.order_id) AS sps_id_0040_0009,
    o.exam_code AS requested_procedure_id_0040_1001,
    sp.protocol_code AS scheduled_protocol_code_0040_0008,
    o.workflow_status
FROM imaging_orders o
JOIN patients p ON o.patient_id = p.patient_id
JOIN exam_catalog ec ON o.exam_code = ec.exam_code
LEFT JOIN staff_users ref ON o.referring_md_id = ref.user_id
LEFT JOIN modalities_and_rooms m ON o.modality_id = m.modality_id
LEFT JOIN scanner_protocols sp ON o.assigned_protocol_id = sp.protocol_id
WHERE o.workflow_status IN ('SCHEDULED', 'ARRIVED_WAITING', 'IN_PREP_SCREENING', 'READY_FOR_SCAN', 'IN_PROGRESS');
DROP VIEW IF EXISTS "vw_radiologist_worklist";
CREATE VIEW vw_radiologist_worklist AS
SELECT 
    o.order_id,
    o.accession_number,
    o.priority,
    o.patient_class,
    p.mrn,
    p.last_name || ', ' || p.first_name AS patient_name,
    ec.modality_type,
    ec.exam_name,
    o.clinical_indication,
    ds.total_instance_count AS slice_count,
    ds.pacs_zero_footprint_viewer_url,
    rad.last_name AS assigned_radiologist,
    CAST((julianday('now') - julianday(o.qa_completed_at)) * 1440 AS INTEGER) AS mins_in_reading_queue
FROM imaging_orders o
JOIN patients p ON o.patient_id = p.patient_id
JOIN exam_catalog ec ON o.exam_code = ec.exam_code
JOIN dicom_studies ds ON o.order_id = ds.order_id
LEFT JOIN staff_users rad ON o.assigned_radiologist_id = rad.user_id
WHERE o.workflow_status IN ('READY_FOR_READ', 'ASSIGNED_READING', 'PRELIMINARY_REPORTED')
ORDER BY 
    CASE o.priority 
        WHEN 'CODE_STROKE_TRAUMA' THEN 1 
        WHEN 'STAT' THEN 2 
        WHEN 'URGENT' THEN 3 
        ELSE 4 
    END,
    o.qa_completed_at ASC;
DROP VIEW IF EXISTS "vw_unacknowledged_critical_alerts";
CREATE VIEW vw_unacknowledged_critical_alerts AS
SELECT 
    cfe.escalation_id,
    o.accession_number,
    cfe.alert_tier,
    p.first_name || ' ' || p.last_name AS patient_name,
    ref_md.first_name || ' ' || ref_md.last_name AS target_referring_md,
    ref_md.mobile_or_pager AS md_direct_pager,
    ref_md.work_phone AS md_phone,
    r.impression_section AS critical_impression,
    cfe.initiated_at,
    cfe.sla_deadline_at,
    CAST((julianday(cfe.sla_deadline_at) - julianday('now')) * 1440 AS INTEGER) AS mins_remaining_until_sla_breach
FROM critical_findings_escalations cfe
JOIN imaging_orders o ON cfe.order_id = o.order_id
JOIN patients p ON o.patient_id = p.patient_id
JOIN radiology_reports r ON cfe.report_id = r.report_id
JOIN staff_users ref_md ON cfe.target_provider_id = ref_md.user_id
WHERE cfe.escalation_status != 'ACKNOWLEDGED_CLOSED';
DROP VIEW IF EXISTS "vw_waiting_room_queue";
CREATE VIEW vw_waiting_room_queue AS
SELECT 
    o.queue_ticket_number,
    o.accession_number,
    p.first_name || ' ' || p.last_name AS patient_name,
    o.patient_class,
    o.priority,
    m.room_name,
    ec.exam_name,
    o.workflow_status,
    COALESCE(s.screening_clearance_status, 'NOT_STARTED') AS safety_screening,
    CAST((julianday('now') - julianday(o.arrived_at)) * 1440 AS INTEGER) AS minutes_waiting
FROM imaging_orders o
JOIN patients p ON o.patient_id = p.patient_id
JOIN exam_catalog ec ON o.exam_code = ec.exam_code
LEFT JOIN modalities_and_rooms m ON o.modality_id = m.modality_id
LEFT JOIN exam_safety_screenings s ON o.order_id = s.order_id
WHERE o.workflow_status IN ('ARRIVED_WAITING', 'IN_PREP_SCREENING', 'READY_FOR_SCAN', 'IN_PROGRESS')
ORDER BY 
    CASE o.priority 
        WHEN 'CODE_STROKE_TRAUMA' THEN 1 
        WHEN 'STAT' THEN 2 
        WHEN 'URGENT' THEN 3 
        ELSE 4 
    END,
    o.arrived_at ASC;
DROP INDEX IF EXISTS "idx_dicom_instances_study";
CREATE INDEX "idx_dicom_instances_study" ON "dicom_instances" (
	"study_id",
	"series_id",
	"instance_number"
);
DROP INDEX IF EXISTS "idx_dispatch_status";
CREATE INDEX "idx_dispatch_status" ON "correspondence_dispatch_queue" (
	"delivery_status",
	"queued_at"
);
DROP INDEX IF EXISTS "idx_escalations_status";
CREATE INDEX "idx_escalations_status" ON "critical_findings_escalations" (
	"escalation_status",
	"sla_deadline_at"
);
DROP INDEX IF EXISTS "idx_orders_patient";
CREATE INDEX "idx_orders_patient" ON "imaging_orders" (
	"patient_id"
);
DROP INDEX IF EXISTS "idx_orders_radiologist";
CREATE INDEX "idx_orders_radiologist" ON "imaging_orders" (
	"assigned_radiologist_id",
	"workflow_status"
);
DROP INDEX IF EXISTS "idx_orders_status_priority";
CREATE INDEX "idx_orders_status_priority" ON "imaging_orders" (
	"workflow_status",
	"priority",
	"arrived_at"
);
DROP INDEX IF EXISTS "idx_report_typst_order";
CREATE INDEX "idx_report_typst_order" ON "report_typst_documents" (
	"order_id"
);
DROP INDEX IF EXISTS "idx_reports_order";
CREATE INDEX "idx_reports_order" ON "radiology_reports" (
	"order_id",
	"report_status"
);
DROP INDEX IF EXISTS "idx_rsna_mod_anat_path";
CREATE INDEX "idx_rsna_mod_anat_path" ON "rsna_reporting_templates" (
	"modality",
	"anatomy",
	"pathology"
);
DROP INDEX IF EXISTS "idx_slots_modality_time";
CREATE INDEX "idx_slots_modality_time" ON "appointment_slots" (
	"modality_id",
	"slot_start_time",
	"slot_end_time"
);
DROP TRIGGER IF EXISTS "trg_audit_order_workflow_changes";
CREATE TRIGGER trg_audit_order_workflow_changes
AFTER UPDATE OF workflow_status ON imaging_orders
FOR EACH ROW
WHEN OLD.workflow_status IS NOT NEW.workflow_status
BEGIN
    INSERT INTO workflow_state_history (order_id, previous_status, new_status, transition_timestamp)
    VALUES (NEW.order_id, OLD.workflow_status, NEW.workflow_status, datetime('now'));
END;
DROP TRIGGER IF EXISTS "trg_enforce_pre_scan_safety_clearance";
CREATE TRIGGER trg_enforce_pre_scan_safety_clearance
BEFORE UPDATE OF workflow_status ON imaging_orders
FOR EACH ROW
WHEN NEW.workflow_status = 'IN_PROGRESS' AND NOT EXISTS (
    SELECT 1 FROM exam_safety_screenings
    WHERE order_id = NEW.order_id
      AND verified_two_patient_identifiers = 1
      AND screening_clearance_status = 'CLEARED'
)
BEGIN
    SELECT RAISE(ABORT, 'SAFETY GATE VIOLATION: Cannot start scan (IN_PROGRESS) without a CLEARED exam_safety_screenings record and verified two-patient identifiers.');
END;
DROP TRIGGER IF EXISTS "trg_fts_reports_sync_insert";
CREATE TRIGGER trg_fts_reports_sync_insert
AFTER INSERT ON radiology_reports
BEGIN
    INSERT INTO fts_radiology_reports(rowid, accession_number, findings_section, impression_section)
    SELECT NEW.report_id, o.accession_number, NEW.findings_section, NEW.impression_section
    FROM imaging_orders o WHERE o.order_id = NEW.order_id;
END;
DROP TRIGGER IF EXISTS "trg_immutable_signed_reports";
CREATE TRIGGER trg_immutable_signed_reports
BEFORE UPDATE ON radiology_reports
FOR EACH ROW
WHEN OLD.report_status = 'FINAL' AND (
    OLD.findings_section IS NOT NEW.findings_section OR
    OLD.impression_section IS NOT NEW.impression_section
)
BEGIN
    SELECT RAISE(ABORT, 'COMPLIANCE LOCK: Signed FINAL reports are legally immutable. Create a new row with report_status = ADDENDUM and parent_report_id pointing to this report.');
END;
DROP TRIGGER IF EXISTS "trg_on_report_finalized";
CREATE TRIGGER trg_on_report_finalized
AFTER insert ON radiology_reports
FOR EACH ROW
WHEN NEW.report_status IN ('PRELIMINARY', 'FINAL')
BEGIN
    -- Update parent order status
    UPDATE imaging_orders
    SET workflow_status = CASE WHEN NEW.report_status = 'FINAL' THEN 'FINAL_REPORTED' ELSE 'PRELIMINARY_REPORTED' END,
        final_reported_at = CASE WHEN NEW.report_status = 'FINAL' THEN datetime('now') ELSE final_reported_at END
    WHERE order_id = NEW.order_id;

    -- Queue Outbound HL7 / Fax Report Dispatch to Referring MD
    INSERT INTO correspondence_dispatch_queue (
        order_id, report_id, patient_id, recipient_user_id, recipient_type, channel, destination_endpoint, message_type, payload_content
    )
    SELECT 
        o.order_id, NEW.report_id, o.patient_id, o.referring_md_id, 'REFERRING_MD',
        CASE WHEN u.preferred_comm_channel = 'FAX' THEN 'FAX' ELSE 'HL7_ORU_R01' END,
        COALESCE(u.fax_number, u.email, 'MIRTH_HL7_ENGINE_PORT_6661'),
        NEW.report_status || '_REPORT',
        'ACCESSION: ' || o.accession_number || ' | IMPRESSION: ' || NEW.impression_section
    FROM imaging_orders o
    JOIN staff_users u ON o.referring_md_id = u.user_id
    WHERE o.order_id = NEW.order_id;

    -- If Critical/Unexpected Finding is flagged, automatically open a Closed-Loop Escalation Ticket
    INSERT INTO critical_findings_escalations (
        report_id, order_id, alert_tier, sla_deadline_at, initiated_by_rad_id, target_provider_id
    )
    SELECT 
        NEW.report_id, NEW.order_id,
        CASE 
            WHEN NEW.acr_actionable_category = 'CAT1_IMMEDIATE_LIFE_THREAT_1HR' THEN 'CAT1_STAT_1HR'
            WHEN NEW.acr_actionable_category = 'CAT2_URGENT_WITHIN_24HR' THEN 'CAT2_URGENT_24HR'
            ELSE 'CAT3_ACTIONABLE_72HR'
        END,
        CASE 
            WHEN NEW.acr_actionable_category = 'CAT1_IMMEDIATE_LIFE_THREAT_1HR' THEN datetime('now', '+1 hour')
            WHEN NEW.acr_actionable_category = 'CAT2_URGENT_WITHIN_24HR' THEN datetime('now', '+24 hours')
            ELSE datetime('now', '+72 hours')
        END,
        NEW.signing_radiologist_id,
        o.referring_md_id
    FROM imaging_orders o
    WHERE o.order_id = NEW.order_id AND NEW.is_critical_or_unexpected = 1;
END;
DROP TRIGGER IF EXISTS "trg_on_report_status_updated";
CREATE TRIGGER trg_on_report_status_updated
AFTER UPDATE OF report_status ON radiology_reports
FOR EACH ROW
WHEN OLD.report_status = 'DRAFT' AND NEW.report_status IN ('PRELIMINARY', 'FINAL')
BEGIN
    UPDATE imaging_orders
    SET workflow_status = CASE WHEN NEW.report_status = 'FINAL' THEN 'FINAL_REPORTED' ELSE 'PRELIMINARY_REPORTED' END,
        final_reported_at = CASE WHEN NEW.report_status = 'FINAL' THEN datetime('now') ELSE final_reported_at END
    WHERE order_id = NEW.order_id;

    INSERT INTO correspondence_dispatch_queue (
        order_id, report_id, patient_id, recipient_user_id, recipient_type, channel, destination_endpoint, message_type, payload_content
    )
    SELECT 
        o.order_id, NEW.report_id, o.patient_id, o.referring_md_id, 'REFERRING_MD',
        CASE WHEN u.preferred_comm_channel = 'FAX' THEN 'FAX' ELSE 'HL7_ORU_R01' END,
        COALESCE(u.fax_number, u.email, 'MIRTH_HL7_ENGINE_PORT_6661'),
        NEW.report_status || '_REPORT',
        'ACCESSION: ' || o.accession_number || ' | IMPRESSION: ' || NEW.impression_section
    FROM imaging_orders o
    JOIN staff_users u ON o.referring_md_id = u.user_id
    WHERE o.order_id = NEW.order_id;

    INSERT INTO critical_findings_escalations (
        report_id, order_id, alert_tier, sla_deadline_at, initiated_by_rad_id, target_provider_id
    )
    SELECT 
        NEW.report_id, NEW.order_id,
        CASE 
            WHEN NEW.acr_actionable_category = 'CAT1_IMMEDIATE_LIFE_THREAT_1HR' THEN 'CAT1_STAT_1HR'
            WHEN NEW.acr_actionable_category = 'CAT2_URGENT_WITHIN_24HR' THEN 'CAT2_URGENT_24HR'
            ELSE 'CAT3_ACTIONABLE_72HR'
        END,
        CASE 
            WHEN NEW.acr_actionable_category = 'CAT1_IMMEDIATE_LIFE_THREAT_1HR' THEN datetime('now', '+1 hour')
            WHEN NEW.acr_actionable_category = 'CAT2_URGENT_WITHIN_24HR' THEN datetime('now', '+24 hours')
            ELSE datetime('now', '+72 hours')
        END,
        NEW.signing_radiologist_id,
        o.referring_md_id
    FROM imaging_orders o
    WHERE o.order_id = NEW.order_id AND NEW.is_critical_or_unexpected = 1;
END;
DROP TRIGGER IF EXISTS "trg_post_remittance_to_claim_header";
CREATE TRIGGER trg_post_remittance_to_claim_header
AFTER INSERT ON billing_remittance_transactions
FOR EACH ROW
BEGIN
    UPDATE billing_claim_headers
    SET total_payer_paid = total_payer_paid + CASE 
            WHEN NEW.tx_type = 'PAYMENT' AND NEW.tx_source IN ('PRIMARY_PAYER_ERA_835', 'SECONDARY_PAYER_EOB') THEN NEW.amount ELSE 0.00 END,
        total_patient_paid = total_patient_paid + CASE 
            WHEN NEW.tx_type = 'PAYMENT' AND NEW.tx_source = 'PATIENT_POS_RECEIPT' THEN NEW.amount ELSE 0.00 END,
        total_contractual_adjustments = total_contractual_adjustments + CASE 
            WHEN NEW.tx_type IN ('CONTRACTUAL_ADJUSTMENT_CO45', 'MPPR_REDUCTION_ADJUSTMENT', 'ADMIN_WRITE_OFF') THEN NEW.amount ELSE 0.00 END,
        patient_responsibility_balance = patient_responsibility_balance + CASE 
            WHEN NEW.tx_type IN ('DEDUCTIBLE_TRANSFER_PR1', 'COINSURANCE_TRANSFER_PR2', 'COPAY_TRANSFER_PR3') THEN NEW.amount
            WHEN NEW.tx_type = 'PAYMENT' AND NEW.tx_source = 'PATIENT_POS_RECEIPT' THEN -NEW.amount
            ELSE 0.00 END
    WHERE claim_id = NEW.claim_id;

    -- Auto-close claim if remaining balance hits 0.00
    UPDATE billing_claim_headers
    SET claim_status = 'PAID_CLOSED_ZERO_BAL',
        closed_at = datetime('now')
    WHERE claim_id = NEW.claim_id
      AND remaining_total_balance = 0.00
      AND total_gross_charges > 0.00;
END;
DROP TRIGGER IF EXISTS "trg_prevent_closed_gl_period_edits";
CREATE TRIGGER trg_prevent_closed_gl_period_edits
BEFORE UPDATE ON billing_claim_headers
FOR EACH ROW
WHEN OLD.is_locked_gl_closed = 1
BEGIN
    SELECT RAISE(ABORT, 'ACCOUNTING LOCK: Cannot modify a claim belonging to a closed General Ledger (GL) financial period.');
END;
DROP TRIGGER IF EXISTS "trg_prevent_modality_double_booking";
CREATE TRIGGER trg_prevent_modality_double_booking
BEFORE INSERT ON appointment_slots
FOR EACH ROW
WHEN NEW.slot_status IN ('BOOKED', 'BLOCKED_MAINTENANCE') AND EXISTS (
    SELECT 1 FROM appointment_slots
    WHERE modality_id = NEW.modality_id
      AND slot_status IN ('BOOKED', 'BLOCKED_MAINTENANCE')
      AND NEW.slot_start_time < slot_end_time
      AND NEW.slot_end_time > slot_start_time
)
BEGIN
    SELECT RAISE(ABORT, 'SCHEDULING CONFLICT: The requested modality room is already booked or blocked during this time window.');
END;
DROP TRIGGER IF EXISTS "trg_prevent_unacknowledged_critical_closure";
CREATE TRIGGER trg_prevent_unacknowledged_critical_closure
BEFORE UPDATE OF workflow_status ON imaging_orders
FOR EACH ROW
WHEN NEW.workflow_status = 'CLOSED' AND EXISTS (
    SELECT 1 FROM critical_findings_escalations
    WHERE order_id = NEW.order_id
      AND (escalation_status != 'ACKNOWLEDGED_CLOSED' OR verbatim_readback_verified = 0)
)
BEGIN
    SELECT RAISE(ABORT, 'CRITICAL SAFETY LOCK: Cannot close order while an unacknowledged critical finding lacks documented physician verbatim read-back.');
END;
DROP TRIGGER IF EXISTS "trg_staff_shift_guard_ins";
CREATE TRIGGER trg_staff_shift_guard_ins
BEFORE INSERT ON staff_shift_schedules
BEGIN
    SELECT RAISE(ABORT, 'Invalid shift duration: end must be after start and <= 24h for working shifts.')
    WHERE NEW.shift_end <= NEW.shift_start
       OR (NEW.shift_type NOT LIKE 'OFF%' AND (julianday(NEW.shift_end) - julianday(NEW.shift_start)) * 24.0 > 24.0);

    SELECT RAISE(ABORT, 'Overlap conflict: staff member already has a shift during this window.')
    WHERE EXISTS (
        SELECT 1 FROM staff_shift_schedules
        WHERE user_id = NEW.user_id
          AND NEW.shift_start < shift_end
          AND NEW.shift_end > shift_start
    );
END;
DROP TRIGGER IF EXISTS "trg_staff_shift_guard_upd";
CREATE TRIGGER trg_staff_shift_guard_upd
BEFORE UPDATE ON staff_shift_schedules
BEGIN
    SELECT RAISE(ABORT, 'Invalid shift duration: end must be after start and <= 24h for working shifts.')
    WHERE NEW.shift_end <= NEW.shift_start
       OR (NEW.shift_type NOT LIKE 'OFF%' AND (julianday(NEW.shift_end) - julianday(NEW.shift_start)) * 24.0 > 24.0);

    SELECT RAISE(ABORT, 'Overlap conflict: staff member already has a shift during this window.')
    WHERE EXISTS (
        SELECT 1 FROM staff_shift_schedules
        WHERE user_id = NEW.user_id
          AND shift_id != NEW.shift_id
          AND NEW.shift_start < shift_end
          AND NEW.shift_end > shift_start
    );
END;
DROP TRIGGER IF EXISTS "trg_sync_claim_header_on_line_insert";
CREATE TRIGGER trg_sync_claim_header_on_line_insert
AFTER INSERT ON billing_claim_lines
FOR EACH ROW
BEGIN
    UPDATE billing_claim_headers
    SET total_gross_charges = (
            SELECT COALESCE(SUM(line_gross_total), 0.00) FROM billing_claim_lines WHERE claim_id = NEW.claim_id
        ),
        total_contractual_expected = (
            SELECT COALESCE(SUM(line_expected_allowed), 0.00) FROM billing_claim_lines WHERE claim_id = NEW.claim_id
        )
    WHERE claim_id = NEW.claim_id;
END;
COMMIT;
