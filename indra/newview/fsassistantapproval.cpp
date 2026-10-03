#include "llviewerprecompiledheaders.h"
#include "fsassistantapproval.h"
#include "llbutton.h"
#include "lltexteditor.h"

#include <sstream>

namespace
{
std::string display(const LLSD& value, bool& shortened)
{
    std::string result = value.isMap() ?
        (value.has("path") ? value["path"].asString() : value["name"].asString()) : value.asString();
    for (char& c : result)
        if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    if (result.size() > 2048)
    {
        result = utf8str_truncate(result, 2048) + "…";
        shortened = true;
    }
    return result;
}
}

FSAssistantApproval::FSAssistantApproval(const LLSD& key) : LLFloater(key) {}

bool FSAssistantApproval::postBuild()
{
    getChild<LLButton>("allow")->setCommitCallback([this](LLUICtrl*, const LLSD&) { answer(true); });
    getChild<LLButton>("deny")->setCommitCallback([this](LLUICtrl*, const LLSD&) { answer(false); });
    getChild<LLButton>("previous")->setCommitCallback([this](LLUICtrl*, const LLSD&) { --mPage; updatePage(); });
    getChild<LLButton>("next")->setCommitCallback([this](LLUICtrl*, const LLSD&) { ++mPage; updatePage(); });
    return true;
}

void FSAssistantApproval::present(const LLSD& summary, std::function<void(bool)> callback)
{
    mCallback = std::move(callback);
    mSummary = summary;
    mPage = 0;
    LLStringUtil::format_map_t args;
    args["COUNT"] = summary["subject_count"].asString();
    childSetValue("heading", getString("action_" + summary["op"].asString(), args));
    updatePage();
    openFloater();
    setFocus(true);
}

void FSAssistantApproval::updatePage()
{
    const S32 total = static_cast<S32>(mSummary["subjects"].size());
    mPage = std::max<S32>(0, std::min<S32>(mPage, std::max<S32>(0, (total - 1) / 50)));
    getChild<LLTextEditor>("details")->setText(describe(mSummary));
    getChild<LLButton>("previous")->setEnabled(mPage > 0);
    getChild<LLButton>("next")->setEnabled((mPage + 1) * 50 < total);
    getChild<LLButton>("previous")->setVisible(total > 50);
    getChild<LLButton>("next")->setVisible(total > 50);
    getChild<LLUICtrl>("page")->setVisible(total > 50);
    LLStringUtil::format_map_t args;
    args["FIRST"] = std::to_string(total ? mPage * 50 + 1 : 0);
    args["LAST"] = std::to_string(std::min<S32>(total, (mPage + 1) * 50));
    args["TOTAL"] = std::to_string(total);
    args["OMITTED"] = std::to_string(total - std::min<S32>(50, total - mPage * 50));
    childSetValue("page", getString("page_count", args));
}

void FSAssistantApproval::answer(bool allow)
{
    auto callback = std::move(mCallback);
    mCallback = {};
    closeFloater();
    if (callback) callback(allow);
}

void FSAssistantApproval::dismiss()
{
    mCallback = {};
    closeFloater();
}

void FSAssistantApproval::onClose(bool)
{
    auto callback = std::move(mCallback);
    mCallback = {};
    if (callback) callback(false);
}

std::string FSAssistantApproval::describe(const LLSD& summary)
{
    bool shortened = false;
    const auto format = [&shortened](const LLSD& value) { return display(value, shortened); };
    std::ostringstream text;
    if (summary.has("target")) text << getString("target") << format(summary["target"]) << "\n";
    if (summary.has("destination")) text << getString("destination") << format(summary["destination"]) << "\n";
    if (summary.has("source")) text << getString("source") << format(summary["source"]) << "\n";
    if (summary.has("copy_policy")) text << getString("copy_policy") << format(summary["copy_policy"]) << "\n";
    if (summary.has("width"))
    {
        text << getString("dimensions") << summary["width"].asInteger() << " × " << summary["height"].asInteger() << "\n";
        text << getString(summary["viewport_only"].asBoolean() ? "viewport" : "whole_window") << "\n";
    }
    if (summary.has("cost")) text << getString("price") << "L$" << summary["cost"].asInteger() << "\n";
    if (summary.has("value")) text << getString("value") << format(summary["value"]) << "\n";
    text << "\n";
    const LLSD& subjects = summary["subjects"];
    const S32 end = std::min<S32>(static_cast<S32>(subjects.size()), (mPage + 1) * 50);
    for (S32 index = mPage * 50; index < end; ++index)
    {
        const LLSD& subject = subjects[index];
        if (subject["name_unavailable"].asBoolean()) text << getString("name_unavailable") << " ";
        text << format(subject["name"]);
        if (subject.has("after_name")) text << " → " << format(subject["after_name"]);
        text << "\n";
        if (subject.has("path")) text << "  " << format(subject["path"]) << "\n";
        if (subject.has("source")) text << "  " << getString("source") << format(subject["source"]) << "\n";
        if (subject.has("destination")) text << "  " << getString("destination") << format(subject["destination"]) << "\n";
        if (subject.has("skip_error")) text << "  " << getString("ineligible") << format(subject["skip_error"]) << "\n";
    }
    for (const char* vector : {"position", "focus"})
    {
        if (!summary.has(vector)) continue;
        text << getString(vector);
        for (const LLSD& number : llsd::inArray(summary[vector])) text << number.asReal() << " ";
        text << "\n";
    }
    for (const LLSD& warning : llsd::inArray(summary["consequences"]))
    {
        text << "\n" << getString(warning.asString()) << "\n";
    }
    if (shortened) text << "\n" << getString("shortened") << "\n";
    return text.str();
}
