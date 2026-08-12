PLUGIN := ps5-activity-tracker.plugin
PLUGIN_TITLE_ID := ACTV00001
PLUGIN_VERSION := 1.41

.PHONY: plugin

plugin: $(PLUGIN)

$(PLUGIN): activity-probe.elf ../tools/make_etahen_plugin.py
	python3 ../tools/make_etahen_plugin.py activity-probe.elf $(PLUGIN) \
		--title-id $(PLUGIN_TITLE_ID) --version $(PLUGIN_VERSION)
