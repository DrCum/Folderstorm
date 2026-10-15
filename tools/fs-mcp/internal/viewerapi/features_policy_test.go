package viewerapi

import "testing"

func TestPrivateFeaturePolicyDoesNotInheritInventoryRead(t *testing.T) {
	for _, raw := range []string{`{}`, `{"permissions":{"read":"allow"}}`, `{"permissions":{"world_read":"ask","chat_read":"invalid"}}`} {
		p, ok := ParseStatusPolicy([]byte(raw))
		if !ok {
			t.Fatal("parse")
		}
		for _, class := range []string{ClassChatRead, ClassWorldRead, ClassFriendsRead, ClassPreferencesRead, ClassPoseRead, ClassGestureRead} {
			if p.Level(class) != "deny" {
				t.Fatalf("%s gained private access from %s", class, raw)
			}
		}
	}
	p, ok := ParseStatusPolicy([]byte(`{"permissions":{"world_read":"allow","chat_read":"deny","chat_send_im":"ask"},"capabilities":{"world_observation":1,"social_chat":2,"social_contacts":"1","social_events":1}}`))
	if !ok || p.Level(ClassWorldRead) != "allow" || p.Level(ClassChatSendIM) != "ask" || p.WorldObservation != 1 || p.SocialChat != 0 || p.SocialContacts != 0 || p.SocialEvents != 1 {
		t.Fatalf("unexpected policy %+v", p)
	}
}
