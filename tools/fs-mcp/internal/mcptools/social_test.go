package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"testing"
)

func socialTestServer(api *fakeAPI) *Server {
	return &Server{Find: func() ([]discover.Instance, error) {
		in := testInstance(11)
		in.ViewerSession = "social-login"
		return []discover.Instance{in}, nil
	}, NewClient: func(discover.Instance) API { return api }}
}
func socialStatus(permissions string, version int) json.RawMessage {
	return json.RawMessage(`{"policy_generation":1,"capabilities":{"social_chat":` + string(rune('0'+version)) + `,"social_contacts":1,"social_events":1},"permissions":` + permissions + `}`)
}
func TestSocialReadExactSourceAndPrivatePermissions(t *testing.T) {
	for _, a := range []struct {
		session, class string
		denied         bool
	}{{"social-login", "chat_read", false}, {"wrong-login", "chat_read", true}, {"", "chat_read", true}, {"social-login", "friends_read", true}} {
		api := &fakeAPI{status: socialStatus(`{"chat_read":"allow","friends_read":"deny"}`, 1)}
		res, _, _ := socialTestServer(api).socialCall(context.Background(), a.session, 11, "FSChat", "conversations", []string{a.class}, map[string]any{})
		if res.IsError != a.denied {
			t.Fatalf("unexpected source/permission result: %+v %s", a, textOf(res))
		}
		if contains(api.opsCopy(), "FSChat.conversations") == a.denied {
			t.Fatal("denied dispatch crossed adapter boundary")
		}
	}
}
func TestSocialNoLegacyFallback(t *testing.T) {
	for _, status := range []json.RawMessage{json.RawMessage(`{"logged_in":true}`), socialStatus(`{"chat_read":"allow"}`, 0), socialStatus(`{"chat_read":"allow"}`, 2)} {
		api := &fakeAPI{status: status}
		res, _, _ := socialTestServer(api).socialCall(context.Background(), "social-login", 11, "FSChat", "history", []string{"chat_read"}, map[string]any{})
		if !res.IsError || contains(api.opsCopy(), "FSChat.history") {
			t.Fatal("unsupported capability forwarded")
		}
	}
}
func TestSocialTypedReadValidation(t *testing.T) {
	api := &fakeAPI{status: socialStatus(`{"resident_search":"allow","profile_read":"allow","chat_read":"allow"}`, 1)}
	s := socialTestServer(api)
	for _, query := range []string{"x", string([]byte{0xff, 0xff})} {
		r, _, _ := s.residentSearch(context.Background(), nil, socialSearchArgs{ViewerSession: "social-login", Query: query})
		if !r.IsError {
			t.Fatal("invalid UTF-8 query accepted")
		}
	}
	r, _, _ := s.residentProfile(context.Background(), nil, socialResidentArgs{ViewerSession: "social-login", ResidentID: "ambiguous name", Grid: "grid"})
	if !r.IsError {
		t.Fatal("profile accepted ambiguous name")
	}
	r, _, _ = s.chatHistory(context.Background(), nil, socialConversationArgs{ViewerSession: "social-login", ConversationID: "nearby", Limit: 100})
	if r.IsError || !contains(api.opsCopy(), "FSChat.history") {
		t.Fatal("valid exact history rejected")
	}
	if api.lastNamed["conversation_id"] != "nearby" || api.lastNamed["limit"] != 100 {
		t.Fatal("typed material inputs changed")
	}
}

func TestSocialSendsUseExactTypedOperationAndClass(t *testing.T) {
	for _, topic := range []string{"im", "group", "conference", "nearby"} {
		permissions := `{"chat_send_im":"allow","chat_send_group":"allow","chat_send_conference":"allow","chat_send_nearby":"allow"}`
		api := &fakeAPI{status: socialStatus(permissions, 1)}
		id := "11111111-1111-4111-8111-111111111111"
		if topic == "nearby" {
			id = "nearby"
		}
		r, _, _ := socialTestServer(api).chatSend(context.Background(), nil, socialSendArgs{ViewerSession: "social-login", ConversationID: id, Topic: topic, Text: "/20 literal data", ClientRequestID: "intent-1"})
		op := map[string]string{"im": "sendIM", "group": "sendGroup", "conference": "sendConference", "nearby": "sendNearby"}[topic]
		if r.IsError || !contains(api.opsCopy(), "FSChat."+op) || api.lastNamed["text"] != "/20 literal data" {
			t.Fatalf("wrong send boundary: %s %s", topic, textOf(r))
		}
	}
	api := &fakeAPI{status: socialStatus(`{"chat_send_im":"deny","chat_send_nearby":"allow"}`, 1)}
	r, _, _ := socialTestServer(api).chatSend(context.Background(), nil, socialSendArgs{ViewerSession: "social-login", ConversationID: "11111111-1111-4111-8111-111111111111", Topic: "im", Text: "hello", ClientRequestID: "intent-2"})
	if !r.IsError || contains(api.opsCopy(), "FSChat.sendIM") {
		t.Fatal("IM permission bypass")
	}
}
func TestSocialDraftGetNeedsOriginatingRead(t *testing.T) {
	api := &fakeAPI{status: socialStatus(`{"chat_state":"allow","chat_read":"deny"}`, 1)}
	r, _, _ := socialTestServer(api).chatDraftGet(context.Background(), nil, socialConversationArgs{ViewerSession: "social-login", ConversationID: "nearby"})
	if !r.IsError || contains(api.opsCopy(), "FSChat.draftGet") {
		t.Fatal("draft disclosed through state permission")
	}
}
