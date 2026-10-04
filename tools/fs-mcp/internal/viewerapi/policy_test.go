package viewerapi

import "testing"

func TestParseStatusPolicyDefaultsAndIgnoresPermanentDelete(t *testing.T) {
	raw := []byte(`{
		"logged_in": true,
		"policy_generation": 2,
		"permissions": {
			"read": "ask",
			"trash": "deny",
			"purge": "allow",
			"emptyTrash": "ask",
			"wear": "allow"
		}
	}`)
	policy, ok := ParseStatusPolicy(raw)
	if !ok || !policy.Present || policy.Generation != 2 {
		t.Fatalf("policy = %#v ok=%v", policy, ok)
	}
	if policy.Level(ClassRead) != "allow" {
		t.Fatalf("read ask is not a level, got %s", policy.Level(ClassRead))
	}
	if policy.Level(ClassTrash) != "deny" {
		t.Fatalf("trash = %s", policy.Level(ClassTrash))
	}
	if policy.Level(ClassWear) != "allow" {
		t.Fatalf("wear = %s", policy.Level(ClassWear))
	}
	if policy.Level(ClassMove) != "allow" || policy.Level(ClassNoCopy) != "ask" {
		t.Fatalf("defaults move=%s nocopy=%s", policy.Level(ClassMove), policy.Level(ClassNoCopy))
	}
	if _, exists := policy.Levels["purge"]; exists {
		t.Fatal("purge was stored as a preference")
	}
}

func TestMissingPermissionsIsAnOlderViewer(t *testing.T) {
	policy, ok := ParseStatusPolicy([]byte(`{"logged_in":true}`))
	if !ok || policy.Present {
		t.Fatalf("policy = %#v ok=%v", policy, ok)
	}
	if _, ok := ParseStatusPolicy([]byte(`[]`)); ok {
		t.Fatal("a non-object status parsed")
	}
}

func TestBulkReviewCapabilityIsAnExactVersion(t *testing.T) {
	for _, test := range []struct {
		value string
		want  int
	}{
		{"1", 1}, {"0", 0}, {"2", 0}, {"1.5", 0}, {"true", 0}, {`"1"`, 0}, {"null", 0},
	} {
		policy, ok := ParseStatusPolicy([]byte(`{"permissions":{"read":"allow"},"capabilities":{"bulk_inventory_review":` + test.value + `}}`))
		if !ok || policy.BulkInventoryReview != test.want {
			t.Fatalf("version %s parsed as %#v", test.value, policy)
		}
	}
}
