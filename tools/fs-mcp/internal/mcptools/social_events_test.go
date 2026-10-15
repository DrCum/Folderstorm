package mcptools

import (
	"context"
	"testing"
)

func TestSocialEventsRequireEveryOriginatingRead(t *testing.T) {
	for _, topics := range [][]string{{"chat"}, {"contacts"}, {"notifications"}, {"operations"}, {"chat", "contacts", "notifications"}} {
		api := &fakeAPI{status: socialStatus(`{"chat_read":"allow","friends_read":"deny","notifications_read":"allow"}`, 1)}
		r, _, _ := socialTestServer(api).eventsPoll(context.Background(), nil, socialEventArgs{ViewerSession: "social-login", Topics: topics, Start: "retained"})
		denied := false
		for _, topic := range topics {
			denied = denied || topic == "contacts"
		}
		if r.IsError != denied || contains(api.opsCopy(), "FSEvents.poll") == denied {
			t.Fatal("event topic permission bypass", topics, textOf(r))
		}
	}
}
func TestSocialEventsCursorProtocolIsExplicit(t *testing.T) {
	for _, a := range []socialEventArgs{{Topics: []string{"chat"}}, {Topics: []string{"chat"}, Start: "now", Cursor: "cursor"}, {Topics: []string{"all"}, Start: "now"}, {Topics: []string{"chat", "chat"}, Start: "now"}, {Topics: []string{"chat"}, Start: "now", WaitMS: 1}} {
		api := &fakeAPI{status: socialStatus(`{"chat_read":"allow"}`, 1)}
		a.ViewerSession = "social-login"
		r, _, _ := socialTestServer(api).eventsPoll(context.Background(), nil, a)
		if !r.IsError || contains(api.opsCopy(), "FSEvents.poll") {
			t.Fatal("invalid cursor protocol forwarded")
		}
	}
}
func TestSocialNotificationReviewCannotBypassRead(t *testing.T) {
	api := &fakeAPI{status: socialStatus(`{"notification_review":"allow","notifications_read":"deny"}`, 1)}
	r, _, _ := socialTestServer(api).notificationReview(context.Background(), nil, socialNotificationArgs{ViewerSession: "social-login", NotificationID: "11111111-1111-4111-8111-111111111111"})
	if !r.IsError || contains(api.opsCopy(), "FSEvents.reviewNotification") {
		t.Fatal("notification review leaked private data")
	}
}
