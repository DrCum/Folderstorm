"""Verify captured authority reaches the actual queued native read boundaries."""
import pathlib
import re
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
def source(name):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", (ROOT / name).read_text(), flags=re.S)

class AssetFetchBoundaryTests(unittest.TestCase):
    def test_captured_origin_survives_queue_and_every_coroutine_yield(self):
        text=source("llviewerassetstorage.cpp")
        self.assertIn("AssetReadAuthority>(fetch->currentSource(),fs_assistant::asyncTask())",text)
        self.assertIn("gAgent.addRegionChangedCallback([source=mAssistantFetchSource](){++source->regionGeneration;})",text)
        self.assertIn("source && source->alive && gAssetStorage==storage",text)
        enqueue=text[text.index("void LLViewerAssetStorage::enqueueAssetRequest("):text.index("bool LLViewerAssetStorage::assistantRequestPending(")]
        self.assertIn("const auto assistant=req->mAssistant",enqueue)
        self.assertIn("assetRequestCoro(req,uuid,type,callback,data,assistant)",enqueue)
        coro=text[text.index("void LLViewerAssetStorage::assetRequestCoro("):text.index("std::string LLViewerAssetStorage::getAssetURL(")]
        self.assertNotIn("AsyncScope",coro)
        self.assertGreaterEqual(coro.count("if(!originAllowed())return;"),6)
        http=coro.index("httpAdapter->getRawAndSuspend(")
        self.assertLess(coro.rfind("if(!originAllowed())return;",0,http),http)
        self.assertLess(coro.index("if(!originAllowed())return;",http),coro.index("vf.write("))
        udp=coro.index("ttcp->requestTransfer(")
        self.assertLess(coro.rfind("if(!originAllowed())return;",0,udp),udp)

    def test_cancelled_assistant_does_not_cancel_native_duplicate_consumers(self):
        text=source("llviewerassetstorage.cpp")
        drop=text[text.index("void LLViewerAssetStorage::dropDeniedAssistantRequests("):text.index("void LLViewerAssetStorage::capsRecvForRegion(")]
        self.assertIn("request->mAssistant && !request->mAssistant->allowed()",drop)
        self.assertIn("!request->mFetchQueued",drop)
        self.assertIn("enqueueAssetRequest(request)",drop)
        self.assertNotIn("removeAndCallbackPendingDownloads",drop)
        self.assertIn("if(pending==req)",text)
        self.assertIn("viewer && viewer->mAssistant==assistant",text)
        delivery=text[text.index("LLGetAssetCallback assistantAssetCallback("):text.index("std::function<bool()> LLViewerAssetStorage::captureAssetReadGuard(")]
        self.assertIn("fs_assistant::AsyncScope scope(fetch->authority->task()->guard())",delivery)
        self.assertIn("allowed?status:LL_ERR_INSUFFICIENT_PERMISSIONS",delivery)

    def test_item_bound_udp_checks_the_same_captured_authority(self):
        text=(ROOT.parent / "llmessage" / "llassetstorage.cpp").read_text()
        read=text[text.index("void LLAssetStorage::getInvItemAsset("):text.index("void LLAssetStorage::downloadInvItemCompleteCallback(")]
        self.assertIn("captureAssetReadGuard(callback)",read)
        self.assertEqual(read.count("if (read_guard && !read_guard())"),2)
        self.assertLess(read.rindex("if (read_guard && !read_guard())"),read.index("ttcp->requestTransfer("))
        # requestTransfer itself sends synchronously; there is no unguarded
        # second coroutine/queue hidden below the item-bound UDP call.
        transfer=(ROOT.parent / "llmessage" / "lltransfermanager.cpp").read_text()
        request=transfer[transfer.index("void LLTransferTargetChannel::requestTransfer("):transfer.index("void LLTransferTargetChannel::addTransferTarget(")]
        self.assertIn("sendTransferRequest(ttp, source_params, priority)",request)
        self.assertIn("gMessageSystem->sendReliable(mHost)",request)

if __name__=="__main__":unittest.main()
