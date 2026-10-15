"""Checks the native adapter uses production ownership/receipt boundaries."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ContentBoundaries(unittest.TestCase):
    def test_content_snapshots_require_the_complete_native_read(self):
        # LLFileSystem::read returns true for any nonzero fread result. A
        # concurrent truncate must not become a zero-padded editable snapshot.
        service = (ROOT / "fscontentlistener.cpp").read_text()
        self.assertIn("file.getLastBytesRead()!=size", service)
        self.assertLess(service.index("file.getLastBytesRead()!=size"), service.index("snapshot.hash=hash(snapshot.raw)"))
        inventory = (ROOT / "llinventorylistener.cpp").read_text()
        self.assertIn("file.getLastBytesRead()!=file_length", inventory)
        self.assertLess(inventory.index("file.getLastBytesRead()!=file_length"), inventory.index("std::string raw_content(buffer.data(),file_length)"))

    def test_native_editors_retain_pending_source_ownership(self):
        script = (ROOT / "llpreviewscript.cpp").read_text()
        body = script[script.index("bool LLPreviewLSL::assistantSourceBusy()"):script.index("void LLPreviewLSL::saveIfNeeded")]
        for field in ("mLiveFile", "mSaveDialogShown", "mPendingUploads", "mEnableSave", "mWaving", "caching_files", "nativeSourceBusy"):
            self.assertIn(field, body)
        card = (ROOT / "llpreviewnotecard.cpp").read_text()
        body = card[card.index("bool LLPreviewNotecard::assistantSourceBusy()"):card.index("bool LLPreviewNotecard::canClose()")]
        self.assertIn("mLiveFile", body)
        self.assertIn("mSaveDialogShown", body)
        service = (ROOT / "fscontentlistener.cpp").read_text()
        self.assertEqual(service.count("->assistantSourceBusy()"), 4)

    def test_legacy_native_save_holds_item_lease_until_callback_cleanup(self):
        card = (ROOT / "llpreviewnotecard.cpp").read_text()
        context = card[card.index("struct LLSaveNotecardInfo"):card.index("void LLPreviewNotecard::finishInventoryUpload")]
        self.assertIn("std::shared_ptr<void> mPendingSource", context)
        self.assertIn("LLViewerAssetUpload::trackLegacyInventoryUpload(item_id)", context)
        completion = card[card.index("void LLPreviewNotecard::onSaveComplete"):card.index("bool LLPreviewNotecard::handleSaveChangesDialog")]
        self.assertLess(completion.index("updateServer(false,info->mPendingSource)"), completion.index("delete info"))
        inventory = (ROOT / "llviewerinventory.cpp").read_text()
        self.assertIn("AISAPI::completion_t cr = [source_owner]", inventory)
        self.assertIn("new std::shared_ptr<void>(source_owner)", inventory)
        uploads = (ROOT / "llviewerassetupload.cpp").read_text()
        self.assertIn("if(sPendingLegacyInventoryUploads.has(itemId))return true", uploads)

    def test_confirmed_rich_bytes_are_evicted_and_not_exported_generically(self):
        service = (ROOT / "fscontentlistener.cpp").read_text()
        self.assertIn("if(confirmed && write->notecard)LLFileSystem::removeFile(asset,LLAssetType::AT_NOTECARD)", service)
        self.assertNotIn("->acknowledge(", service)

    def test_status_preserves_domain_id_and_complete_approval_source(self):
        bridge = (ROOT / "fseventapibridge.cpp").read_text()
        self.assertIn('if(!body.has("operation_id"))body["operation_id"]=request_id', bridge)
        self.assertIn('body["bridge_operation_id"]=request_id', bridge)
        service = (ROOT / "fscontentlistener.cpp").read_text()
        self.assertIn('operation.summary["replacement_text"]=request["text"]', service)
        approval = (ROOT / "fsassistantapproval.cpp").read_text()
        self.assertIn('summary["replacement_text"].asString()', approval)
        self.assertIn('utf8str_truncate(replacement, 65536)', approval)

if __name__ == "__main__":
    unittest.main()
