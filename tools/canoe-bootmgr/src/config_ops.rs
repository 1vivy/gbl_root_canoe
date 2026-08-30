use crate::config::{
    ConfigDocument, ConfigEntry, ConfigError, DeviceInfoRepair, EntryRequest, MAX_ENTRIES,
    MAX_GENERATION, Role, canonical_image, validate_mode, validate_request,
};

impl ConfigDocument {
    #[must_use]
    pub const fn empty() -> Self {
        Self {
            entries: Vec::new(),
            generation: 0,
            timeout: 5,
            default: None,
            mode: 1,
            devinfo_repair: DeviceInfoRepair::AsNeeded,
            unknown: Vec::new(),
        }
    }

    pub fn parse(bytes: &[u8]) -> Result<Self, ConfigError> {
        crate::config_parse::parse(bytes)
    }

    pub fn serialize(&self) -> Result<Vec<u8>, ConfigError> {
        crate::config_render::serialize(self)
    }

    #[must_use]
    pub fn entry(&self, id: &str) -> Option<&ConfigEntry> {
        self.entries.iter().find(|entry| entry.id == id)
    }

    pub fn upsert(&mut self, request: EntryRequest) -> Result<u32, ConfigError> {
        validate_request(&request)?;
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        let index = self.entries.iter().position(|entry| entry.id == request.id);
        if index.is_none() && self.entries.len() >= MAX_ENTRIES {
            return Err(ConfigError::Invalid(format!(
                "canoe.cfg already holds {MAX_ENTRIES} entries"
            )));
        }
        let effective_global_mode = request.global_mode.unwrap_or(self.mode);
        let mode = request.mode.unwrap_or_else(|| {
            index.map_or(effective_global_mode, |position| {
                self.entries[position].mode
            })
        });
        validate_mode(mode)?;
        if let Some(global_mode) = request.global_mode {
            self.mode = global_mode;
        }
        if let Some(timeout) = request.timeout {
            self.timeout = timeout;
        }
        if let Some(repair) = request.devinfo_repair {
            self.devinfo_repair = repair;
        }
        let entry_id = request.id.clone();
        let entry = ConfigEntry {
            id: request.id,
            title: request.title,
            image: canonical_image(&request.image)?,
            options: request.options,
            mode,
            role: request.role,
            unknown: index.map_or_else(Vec::new, |position| self.entries[position].unknown.clone()),
        };
        match index {
            Some(position) => self.entries[position] = entry,
            None => self.entries.push(entry),
        }
        if request.make_default {
            self.default = Some(entry_id);
        }
        self.bump_generation()
    }

    pub fn remove(&mut self, id: &str) -> Result<u32, ConfigError> {
        let Some(position) = self.entries.iter().position(|entry| entry.id == id) else {
            return Err(ConfigError::Invalid(format!("no such entry: {id}")));
        };
        if self.entries.len() == 1 {
            return Err(ConfigError::Invalid(
                "canoe.cfg would have no usable entry".to_owned(),
            ));
        }
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        self.entries.remove(position);
        self.bump_generation()
    }
    pub fn sync_managed_rows(&mut self, rows: &[ConfigEntry]) -> Result<u32, ConfigError> {
        if rows.is_empty() || rows.len() > MAX_ENTRIES {
            return Err(ConfigError::Invalid(
                "managed rows must contain at least one entry".to_owned(),
            ));
        }
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        let managed = ["android-a", "android-b", "android-backup"];
        for row in rows {
            validate_request(&EntryRequest {
                id: row.id.clone(),
                title: row.title.clone(),
                image: row.image.clone(),
                options: row.options.clone(),
                role: row.role,
                mode: Some(row.mode),
                global_mode: None,
                timeout: None,
                devinfo_repair: None,
                make_default: false,
            })?;
        }
        let previous = self
            .entries
            .iter()
            .filter(|entry| managed.contains(&entry.id.as_str()))
            .map(|entry| (entry.id.clone(), entry.unknown.clone()))
            .collect::<std::collections::HashMap<_, _>>();
        self.entries
            .retain(|entry| !managed.contains(&entry.id.as_str()));
        self.entries.extend(rows.iter().cloned().map(|mut row| {
            if let Some(unknown) = previous.get(&row.id) {
                row.unknown = unknown.clone();
            }
            row
        }));
        if self
            .default
            .as_deref()
            .is_some_and(|id| managed.contains(&id))
        {
            self.default = None;
        }
        self.bump_generation()
    }

    pub fn set_mode(&mut self, id: &str, mode: u8) -> Result<u32, ConfigError> {
        validate_mode(mode)?;
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        let Some(entry) = self.entries.iter_mut().find(|entry| entry.id == id) else {
            return Err(ConfigError::Invalid(format!("no such entry: {id}")));
        };
        entry.mode = mode;
        self.bump_generation()
    }

    pub fn set_default(&mut self, id: &str) -> Result<u32, ConfigError> {
        if self.entry(id).is_none() {
            return Err(ConfigError::Invalid(format!("no such entry: {id}")));
        }
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        self.default = Some(id.to_owned());
        self.bump_generation()
    }

    pub fn clear_default(&mut self) -> Result<u32, ConfigError> {
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        self.default = None;
        self.bump_generation()
    }

    fn bump_generation(&mut self) -> Result<u32, ConfigError> {
        if self.generation == MAX_GENERATION {
            return Err(ConfigError::Invalid(
                "generation cannot be bumped past 4294967295".to_owned(),
            ));
        }
        self.repair_default();
        self.generation += 1;
        Ok(self.generation)
    }

    fn repair_default(&mut self) {
        if self
            .default
            .as_deref()
            .is_some_and(|id| self.entry(id).is_some())
        {
            return;
        }
        self.default = self
            .entries
            .iter()
            .find(|entry| entry.role == Role::Active)
            .or_else(|| self.entries.first())
            .map(|entry| entry.id.clone());
    }
}
