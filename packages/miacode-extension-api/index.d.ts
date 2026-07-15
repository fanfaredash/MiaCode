export interface ExtensionContext {
  extensionPath: string;
  subscriptions: Array<{ dispose(): void }>;
  log(message: string): ApiResult<unknown>;
}

export interface ApiResult<T = unknown> {
  ok: boolean;
  value?: T;
  error?: string;
}

export interface ActiveDocumentSnapshot {
  uri: string;
  languageId: "simai" | string;
  text: string;
  activeDifficultyId: number;
  dirty: boolean;
}

export interface Disposable {
  dispose(): void;
}

export type ApiRisk = "low" | "medium" | "high" | "blocked";
export type ApiStatus = "implemented" | "planned" | "blocked";

export interface ApiDescriptor {
  id: string;
  method: string;
  permission?: string;
  risk: ApiRisk;
  status: ApiStatus;
  description: string;
}

export interface DevtoolsDiagnosis {
  id?: string;
  method?: string;
  descriptor?: ApiDescriptor;
  implemented: boolean;
  requiredPermission?: string;
  extensionId?: string;
  manifestDeclaresPermission: boolean;
  blockedByMethodHook: boolean;
  blockedByPermissionHook: boolean;
}

export interface DevtoolsRecentCall {
  timestamp: string;
  method: string;
  extensionId?: string;
  permission?: string;
  ok: boolean;
  error?: string;
  elapsedMs: number;
  paramsPreview: string;
}

export interface DevtoolsSnapshot {
  extensionId?: string;
  api: ApiDescriptor[];
  openBridgeObjects: OpenBridgeObject[];
  experimentalRawTargets: ExperimentalRawOpenTarget[];
  extensions: Array<Record<string, unknown>>;
  diagnostics: string[];
  recentCalls: DevtoolsRecentCall[];
  eventCallbackCount: number;
  uiContributions?: Array<Record<string, unknown>>;
  uiViews?: Array<Record<string, unknown>>;
}

export interface ApiRequest {
  id: string;
  reason?: string;
  required?: boolean;
  fallback?: string;
  proposedSignature?: string;
}

export interface OpenBridgeMethod {
  name: string;
  hostMethod?: string;
  command?: string;
  permission: string;
  status: ApiStatus;
  description: string;
}

export interface OpenBridgeObject {
  id: string;
  permission: string;
  stability: "open" | "experimentalRaw" | string;
  description: string;
  methods: OpenBridgeMethod[];
  experimentalRaw?: boolean;
  rawAccess?: boolean;
  rawCppObjectsExposed: boolean;
}

export interface ExperimentalRawOpenTarget extends OpenBridgeObject {
  id: string;
  stability: "experimentalRaw" | string;
  category: string;
  reason: string;
  forbidden: false;
  experimentalRaw: true;
  rawAccess: true;
  rawCppObjectsExposed: true;
  legacyName?: "forbiddenTarget" | string;
}

export type ForbiddenOpenTarget = ExperimentalRawOpenTarget;

export interface PetOverlayFrame {
  src?: string;
  image?: string;
  resource?: string;
  durationMs?: number;
}

export interface PetOverlayOptions {
  id?: string;
  image?: string;
  src?: string;
  resource?: string;
  frames?: Array<string | PetOverlayFrame>;
  sprite?: {
    frames?: Array<string | PetOverlayFrame>;
    fps?: number;
    frameDurationMs?: number;
  };
  text?: string;
  position?: {
    x?: number;
    y?: number;
    anchor?: "topLeft" | "topRight" | "bottomLeft" | "bottomRight" | "center" | string;
  };
  anchor?: "topLeft" | "topRight" | "bottomLeft" | "bottomRight" | "center" | string;
  width?: number;
  height?: number;
  size?: number;
  margin?: number;
  opacity?: number;
  draggable?: boolean;
  onClickCommand?: string;
  onDragEndCommand?: string;
  command?: string;
}

export interface MiaCodeApi {
  api: {
    list(): ApiResult<ApiDescriptor[]>;
    has(id: string): ApiResult<boolean>;
    describe(id: string): ApiResult<ApiDescriptor>;
    describeNamespace(namespaceId: string): ApiResult<ApiDescriptor[]>;
    call<T = unknown>(id: string, params?: Record<string, unknown>): ApiResult<T>;
    invoke<T = unknown>(method: string, params?: Record<string, unknown>): ApiResult<T>;
    request(request: ApiRequest): ApiResult;
  };
  devtools: {
    snapshot(): ApiResult<DevtoolsSnapshot>;
    diagnose(target: string | { id?: string; method?: string }): ApiResult<DevtoolsDiagnosis>;
    recentCalls(): ApiResult<DevtoolsRecentCall[]>;
  };
  open: {
    list(): ApiResult<OpenBridgeObject[]>;
    describe(id: string): ApiResult<OpenBridgeObject | ExperimentalRawOpenTarget>;
    call<T = unknown>(objectId: string, method: string, params?: Record<string, unknown>): ApiResult<T>;
    forbiddenTargets(): ApiResult<ExperimentalRawOpenTarget[]>;
    describeForbiddenTarget(id: string): ApiResult<ExperimentalRawOpenTarget>;
  };
  app: {
    getInfo(): ApiResult<Record<string, unknown>>;
    openPreferences(): ApiResult;
    openAboutDialog(): ApiResult;
    reloadExtensions(): ApiResult;
  };
  commands: {
    registerCommand(command: string, callback: () => void): Disposable;
    executeCommand(command: string, args?: unknown): ApiResult;
    executeInternal(command: string, args?: Record<string, unknown>): ApiResult;
    getCommands(): ApiResult<Array<Record<string, unknown>>>;
    getInternalCommands(): ApiResult<Array<Record<string, unknown>>>;
  };
  window: {
    showInformationMessage(message: string): ApiResult;
    showWarningMessage(message: string): ApiResult;
    showErrorMessage(message: string): ApiResult;
    showInputBox(options?: Record<string, unknown>): ApiResult<string>;
    showQuickPick(items: string[], options?: Record<string, unknown>): ApiResult<string>;
    createStatusBarItem(options: { text: string; timeoutMs?: number }): ApiResult;
  };
  workspace: {
    getActiveDocument(): ActiveDocumentSnapshot;
    applyDocumentEdit(edit: { text: string }): ApiResult;
    getChartMetadata(): ApiResult<Record<string, unknown>>;
    updateChartMetadata(patch: Record<string, unknown>): ApiResult;
    getChartFolder(): ApiResult<string>;
    getMediaFiles(): ApiResult<string[]>;
    save(): ApiResult;
    saveAs(path: string): ApiResult;
  };
  events: {
    onDidOpenDocument(callback: (event?: unknown) => void): Disposable;
    onDidSaveDocument(callback: (event?: unknown) => void): Disposable;
    onDidChangeText(callback: (event?: unknown) => void): Disposable;
    onDidChangeSelection(callback: (event?: unknown) => void): Disposable;
  };
  document: {
    getDifficulties(): ApiResult<Array<Record<string, unknown>>>;
    getActiveDifficulty(): ApiResult<Record<string, unknown>>;
    setActiveDifficulty(id: number): ApiResult;
    replaceActiveDifficultyText(text: string): ApiResult;
    query(selector?: { select?: string[] | "*" }): ApiResult<Record<string, unknown>>;
    edit(request: { ops?: Array<Record<string, unknown>> } | Array<Record<string, unknown>> | Record<string, unknown>): ApiResult<{ applied: number }>;
    getParsedNoteMarkers(): ApiResult<Array<Record<string, unknown>>>;
    getTimingMetadata(): ApiResult<Record<string, unknown>>;
    applyTextEdits(edits: Array<{ start: number; end: number; text: string }>): ApiResult;
    format(): ApiResult;
    createDifficulty(options: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    deleteDifficulty(id: number): ApiResult;
    renameDifficulty(id: number, label: string): ApiResult;
  };
  editor: {
    undo(): ApiResult;
    redo(): ApiResult;
    cut(): ApiResult;
    copy(): ApiResult;
    paste(): ApiResult;
    selectAll(): ApiResult;
    getSelection(): ApiResult<Record<string, number>>;
    getCursor(): ApiResult<Record<string, number>>;
    getText(): ApiResult<string>;
    getVisibleRange(): ApiResult<Record<string, number>>;
    revealRange(range: Record<string, unknown>): ApiResult;
    getParsedSnapshot(): ApiResult<Record<string, unknown>>;
    setSelection(range: { start: number; end: number }): ApiResult;
    getLine(line: number): ApiResult<Record<string, unknown>>;
    getCurrentLine(): ApiResult<Record<string, unknown>>;
    getCurrentToken(): ApiResult<Record<string, unknown>>;
    insertText(text: string): ApiResult;
    replaceSelection(text: string): ApiResult;
    replaceRange(range: Record<string, unknown>, text: string): ApiResult;
    addDecoration(range: Record<string, unknown>, options?: Record<string, unknown>): ApiResult;
    clearDecorations(ownerId?: string): ApiResult;
    registerHoverProvider(provider: Record<string, unknown>): Disposable;
    registerCompletionProvider(provider: Record<string, unknown>): Disposable;
    registerCodeActionProvider(provider: Record<string, unknown>): Disposable;
    getRegisteredProviders(kind?: string): ApiResult<Array<Record<string, unknown>>>;
    collectHover(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    collectCompletions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    collectCodeActions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    showHover(context?: Record<string, unknown>, markdown?: string): ApiResult<Record<string, unknown>>;
    showCompletions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    showCodeActions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
  };
  providers: {
    registerHoverProvider(provider: Record<string, unknown>): Disposable;
    registerCompletionProvider(provider: Record<string, unknown>): Disposable;
    registerCodeActionProvider(provider: Record<string, unknown>): Disposable;
    getRegisteredProviders(kind?: string): ApiResult<Array<Record<string, unknown>>>;
    collectHover(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    collectCompletions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    collectCodeActions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    showHover(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    showCompletions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    showCodeActions(context?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
  };
  validation: {
    run(): ApiResult;
    getLastResult(): ApiResult<Record<string, unknown>>;
    addDiagnostics(ownerId: string, diagnostics: Array<Record<string, unknown>>): ApiResult;
    clearDiagnostics(ownerId?: string): ApiResult;
  };
  diagnostics: {
    validateDocument(): ApiResult;
  };
  timeline: {
    getSnapshot(): ApiResult<Record<string, unknown>>;
    getCurrentSecond(): ApiResult<number>;
    getZoomState(): ApiResult<Record<string, unknown>>;
    getVisibleRange(): ApiResult<Record<string, unknown>>;
    getMarkersAtSecond(second: number, options?: Record<string, unknown>): ApiResult<Array<Record<string, unknown>>>;
    seek(second: number): ApiResult;
    zoomIn(options?: Record<string, unknown>): ApiResult;
    zoomOut(options?: Record<string, unknown>): ApiResult;
    stepZoomPreset(delta: number, options?: Record<string, unknown>): ApiResult;
    setZoomScale(scale: number, options?: Record<string, unknown>): ApiResult;
    scrollToSecond(second: number): ApiResult;
    setFollowPreview(enabled: boolean): ApiResult;
    setFollowProgress(enabled: boolean): ApiResult;
    addMarker(marker: Record<string, unknown>): ApiResult;
    clearMarkers(ownerId?: string): ApiResult;
    addBand(band: Record<string, unknown>): ApiResult;
    addVerticalLine(line: Record<string, unknown>): ApiResult;
    clearVisuals(ownerId?: string): ApiResult;
  };
  preview: {
    play(): ApiResult;
    pause(): ApiResult;
    stop(): ApiResult;
    seek(second: number): ApiResult;
    getState(): ApiResult<Record<string, unknown>>;
    getRenderState(): ApiResult<Record<string, unknown>>;
    setSpeed(value: number): ApiResult;
    addOverlay(overlay: Record<string, unknown>): ApiResult;
    updateOverlay(id: string, patch: Record<string, unknown>): ApiResult;
    removeOverlay(id?: string, ownerId?: string): ApiResult<{ removed: number }>;
    clearOverlays(ownerId?: string): ApiResult;
    getOverlays(): ApiResult<Array<Record<string, unknown>>>;
    renderOverlayLayer(): ApiResult<{ count: number }>;
    hitTestOverlay(x: number, y: number): ApiResult<Array<Record<string, unknown>>>;
    onFrame(callback: (event?: unknown) => void): Disposable;
  };
  media: {
    getInfo(): ApiResult<Record<string, unknown>>;
    list(): ApiResult<Array<Record<string, unknown>>>;
    listMedia(): ApiResult<Array<Record<string, unknown>>>;
    getAssetPath(id: string): ApiResult<string>;
    getMediaAssetPath(id: string): ApiResult<string>;
  };
  theme: {
    getCurrent(): ApiResult<string>;
    listAvailable(): ApiResult<string[]>;
    getColor(role?: string): ApiResult<string | Record<string, unknown>>;
    setCurrent(theme: string): ApiResult;
  };
  backup: {
    list(): ApiResult<Array<Record<string, unknown>>>;
    listBackups(): ApiResult<Array<Record<string, unknown>>>;
    createBackup(options?: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    readBackup(id: string): ApiResult<Record<string, unknown>>;
    removeBackup(id: string): ApiResult;
  };
  shortcuts: {
    list(): ApiResult<Record<string, unknown>>;
    listShortcuts(): ApiResult<Record<string, unknown>>;
    getEditableShortcuts(): ApiResult<Array<Record<string, unknown>>>;
    getKeybinding(command: string): ApiResult<string>;
    registerShortcut(command: string, keybinding: string): ApiResult;
    registerCommandShortcut(shortcut: Record<string, unknown>): ApiResult;
  };
  input: {
    registerWheelGesture(gesture: {
      id?: string;
      target?: "any" | "timeline" | "preview" | "editor" | string;
      modifiers?: string[];
      direction?: "any" | "up" | "down" | string;
      command: string;
    }): ApiResult<Record<string, unknown>>;
    registerKeyGesture(gesture: {
      id?: string;
      target?: "any" | "timeline" | "preview" | "editor" | string;
      modifiers?: string[];
      phase?: "any" | "press" | "release" | string;
      key?: string;
      command: string;
    }): ApiResult<Record<string, unknown>>;
    registerMouseGesture(gesture: {
      id?: string;
      target?: "any" | "timeline" | "preview" | "editor" | string;
      modifiers?: string[];
      phase?: "any" | "press" | "release" | "doubleClick" | string;
      button?: "any" | "left" | "right" | "middle" | string;
      command: string;
    }): ApiResult<Record<string, unknown>>;
    getGestures(): ApiResult<Array<Record<string, unknown>>>;
  };
  ui: {
    registerSidebarView(view: Record<string, unknown>): Disposable;
    registerBottomTabView(view: Record<string, unknown>): Disposable;
    registerPreferencesPage(page: Record<string, unknown>): Disposable;
    registerFloatingPanel(panel: Record<string, unknown>): Disposable;
    registerToolbarButton(button: Record<string, unknown>): Disposable;
    registerPetOverlay(overlay: PetOverlayOptions): ApiResult<Record<string, unknown>>;
    registerSceneOverlay(overlay: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    getContributions(): ApiResult<Array<Record<string, unknown>>>;
    getViews(): ApiResult<Array<Record<string, unknown>>>;
    unregisterView(id: string, ownerId?: string): ApiResult<{ removed: number }>;
    refreshViews(): ApiResult<{ count: number }>;
    renderDeclarativeView(view: Record<string, unknown>): ApiResult;
    renderSidebarView(view: Record<string, unknown>): ApiResult;
    renderBottomTabView(view: Record<string, unknown>): ApiResult;
    renderPreferencesPage(page: Record<string, unknown>): ApiResult;
    renderFloatingPanel(panel: Record<string, unknown>): ApiResult;
    renderToolbarButton(button: Record<string, unknown>): ApiResult;
    renderSceneOverlay(overlay: Record<string, unknown>): ApiResult;
    renderWebView(view: Record<string, unknown>): ApiResult;
    renderCanvasView(view: Record<string, unknown>): ApiResult;
  };
  export: {
    getPresets(): ApiResult<Array<Record<string, unknown>>>;
    registerPreset(preset: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    registerHook(hook: Record<string, unknown>): Disposable;
    registerBeforeExportHook(hook: Record<string, unknown>): Disposable;
    registerAfterExportHook(hook: Record<string, unknown>): Disposable;
    registerCoverTemplate(templateSpec: Record<string, unknown>): Disposable;
    registerBatchJobProvider(provider: Record<string, unknown>): Disposable;
    startVideoExport(): ApiResult<Record<string, unknown>>;
    startCoverExport(): ApiResult<Record<string, unknown>>;
  };
  logs: {
    append(channel: string, message: string): ApiResult;
    getPath(channel?: string): ApiResult<string>;
    open(channel?: string): ApiResult;
  };
  extensions: {
    all(): ApiResult<Array<Record<string, unknown>>>;
    get(id: string): ApiResult<Record<string, unknown>>;
    enable(id: string): ApiResult;
    disable(id: string): ApiResult;
    installFromFolder(path: string): ApiResult;
    remove(id: string): ApiResult;
  };
}

export function activate(context: ExtensionContext): void;
export function deactivate(): void;

declare global {
  const miacode: MiaCodeApi;
}
