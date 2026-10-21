package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"github.com/modelcontextprotocol/go-sdk/mcp"
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
	api = &fakeAPI{status: socialStatus(`{"chat_state":"deny","chat_read":"allow"}`, 1)}
	r, _, _ = socialTestServer(api).chatDraftGet(context.Background(), nil, socialConversationArgs{ViewerSession: "social-login", ConversationID: "nearby"})
	if r.IsError || !contains(api.opsCopy(), "FSChat.draftGet") {
		t.Fatal("private draft read incorrectly requires mutation permission")
	}
}
func TestSocialRegisteredAdapters(t *testing.T) {
	api := &fakeAPI{status: socialStatus(`{"chat_read":"allow","chat_state":"allow","chat_send_im":"allow","chat_send_group":"allow","chat_send_conference":"allow","chat_send_nearby":"allow","friends_read":"allow","resident_search":"allow","profile_read":"allow","notifications_read":"allow","notification_review":"allow"}`, 1)}
	server := mcp.NewServer(&mcp.Implementation{Name: "social-tests", Version: "1"}, nil)
	state := socialTestServer(api)
	registerSocial(server, state)
	registerSocialEvents(server, state)
	ct, st := mcp.NewInMemoryTransports()
	ctx := context.Background()
	if _, err := server.Connect(ctx, st, nil); err != nil {
		t.Fatal(err)
	}
	client := mcp.NewClient(&mcp.Implementation{Name: "social-client", Version: "1"}, nil)
	cs, err := client.Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer cs.Close()
	resident := "11111111-1111-4111-8111-111111111111"
	tests := []struct {
		name, op string
		args     map[string]any
	}{
		{"chat_conversations", "FSChat.conversations", map[string]any{}},
		{"chat_history", "FSChat.history", map[string]any{"conversation_id": "nearby"}},
		{"chat_search", "FSChat.search", map[string]any{"query": "hello"}},
		{"friends_list", "FSContacts.friends", map[string]any{}},
		{"resident_search", "FSContacts.search", map[string]any{"query": "hello"}},
		{"resident_profile", "FSContacts.profile", map[string]any{"resident_id": resident, "grid": "grid"}},
		{"chat_open_im", "FSChat.open", map[string]any{"resident_id": resident, "grid": "grid"}},
		{"chat_draft_get", "FSChat.draftGet", map[string]any{"conversation_id": "nearby"}},
		{"chat_draft_set", "FSChat.draftSet", map[string]any{"conversation_id": "nearby", "text": "draft", "expected_revision": "0"}},
		{"chat_send", "FSChat.sendNearby", map[string]any{"conversation_id": "nearby", "topic": "nearby", "text": "hello", "client_request_id": "intent"}},
		{"chat_mark_read", "FSChat.markRead", map[string]any{"conversation_id": resident}},
		{"chat_close", "FSChat.close", map[string]any{"conversation_id": resident}},
		{"events_poll", "FSEvents.poll", map[string]any{"topics": []string{"chat", "contacts", "notifications"}, "start": "retained"}},
		{"notifications_list", "FSEvents.notifications", map[string]any{}},
		{"notifications_get", "FSEvents.getNotification", map[string]any{"notification_id": resident}},
		{"notification_review", "FSEvents.reviewNotification", map[string]any{"notification_id": resident}},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			test.args["viewer_session"] = "social-login"
			r := call(t, cs, test.name, test.args)
			if r.IsError || !contains(api.opsCopy(), test.op) {
				t.Fatalf("registered schema/adapter failed: %s", textOf(r))
			}
		})
	}
	tools, err := cs.ListTools(ctx, nil)
	if err != nil {
		t.Fatal(err)
	}
	for _, tool := range tools.Tools {
		if tool.Annotations.OpenWorldHint == nil || !*tool.Annotations.OpenWorldHint {
			t.Fatal("social effect mislabeled local-only", tool.Name)
		}
	}
}
