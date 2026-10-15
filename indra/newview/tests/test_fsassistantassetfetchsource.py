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
        self.assertIn("tpvf.setReadGuard(read_guard)",read)

    def test_udp_packets_and_shared_cache_publication_recheck_captured_origin(self):
        text=(ROOT.parent / "llmessage" / "lltransfertargetvfile.cpp").read_text()
        data=text[text.index("LLTSCode LLTransferTargetVFile::dataCallback("):text.index("bool LLTransferTargetVFile::readAllowed()")]
        self.assertLess(data.index("if(!readAllowed())return LLTS_INSUFFICIENT_PERMISSIONS"),data.index("LLFileSystem vf("))
        complete=text[text.index("void LLTransferTargetVFile::completionCallback("):]
        self.assertLess(complete.index("readAllowed()?status:LLTS_INSUFFICIENT_PERMISSIONS"),complete.index("file.rename("))
        self.assertEqual(complete.count("switch (completion_status)"),2)
        self.assertIn("vf.remove()",complete)
        allowed=text[text.index("bool LLTransferTargetVFile::readAllowed()"):text.index("void LLTransferTargetVFile::completionCallback(")]
        self.assertIn("if(mReadDenied)return false",allowed)
        self.assertIn("mReadDenied=true",allowed)
        manager=(ROOT.parent / "llmessage" / "lltransfermanager.cpp").read_text()
        self.assertEqual(manager.count("if(ret_code==LLTS_INSUFFICIENT_PERMISSIONS)status=ret_code"),2)
        viewer=source("llviewerassetstorage.cpp")
        self.assertIn("tpvf.setReadGuard([assistant](){return assistant->allowed();})",viewer)
        self.assertIn("else tpvf.setCallback(downloadCompleteCallback,*req)",viewer)
        completion=viewer[viewer.index("tpvf.setCallback([this,assistant]"):viewer.index("else tpvf.setCallback(downloadCompleteCallback,*req)")]
        self.assertLess(completion.index("dropDeniedAssistantRequests(file_id,type)"),completion.index("downloadCompleteCallback(result,file_id,type,request,ext)"))

if __name__=="__main__":unittest.main()
