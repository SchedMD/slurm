from gitlint.rules import ConfigurationRule


class IgnoreConfigurationRule(ConfigurationRule):
    """
    This rules allows us to ignore all but the user defined rules for now.
    """

    # A rule MUST have a human friendly name
    name = "ignore-default-configuration-rule"

    # A rule MUST have a *unique* id
    # We recommend starting with UCR (for User-defined Configuration-Rule)
    id = "UCR1"

    def apply(self, config, commit):
        for rule in config.rules:
            self.log.debug(f"rule id:{rule.id} name:{rule.name}")
            if rule.id != "T3" and not rule.id.startswith("UC"):
                self.log.debug(f"ignoring rule id:{rule.id} name:{rule.name}")
                config.ignore.append(rule.name)
