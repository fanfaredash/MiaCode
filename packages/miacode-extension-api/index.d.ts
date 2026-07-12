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

export interface ApiRequest {
  id: string;
  reason?: string;
  required?: boolean;
  fallback?: string;
  proposedSignature?: string;
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
  app: {
    getInfo(): ApiResult<Record<string, unknown>>;
    openPreferences(): ApiResult;
    reloadExtensions(): ApiResult;
  };
  commands: {
    registerCommand(command: string, callback: () => void): Disposable;
    executeCommand(command: string, args?: unknown): ApiResult;
    executeInternal(command: string, args?: Record<string, unknown>): ApiResult;
    getCommands(): ApiResult<Array<Record<string, unknown>>>;
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
    getSelection(): ApiResult<Record<string, number>>;
    getCursor(): ApiResult<Record<string, number>>;
    setSelection(range: { start: number; end: number }): ApiResult;
    getLine(line: number): ApiResult<Record<string, unknown>>;
    getCurrentLine(): ApiResult<Record<string, unknown>>;
    getCurrentToken(): ApiResult<Record<string, unknown>>;
    insertText(text: string): ApiResult;
    replaceSelection(text: string): ApiResult;
    replaceRange(range: Record<string, unknown>, text: string): ApiResult;
    addDecoration(range: Record<string, unknown>, options?: Record<string, unknown>): ApiResult;
    clearDecorations(ownerId?: string): ApiResult;
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
    seek(second: number): ApiResult;
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
    setSpeed(value: number): ApiResult;
    addOverlay(overlay: Record<string, unknown>): ApiResult;
    updateOverlay(id: string, patch: Record<string, unknown>): ApiResult;
    removeOverlay(id?: string, ownerId?: string): ApiResult<{ removed: number }>;
    clearOverlays(ownerId?: string): ApiResult;
    getOverlays(): ApiResult<Array<Record<string, unknown>>>;
    renderOverlayLayer(): ApiResult<{ count: number }>;
    hitTestOverlay(x: number, y: number): ApiResult<Array<Record<string, unknown>>>;
  };
  ui: {
    registerBottomTabView(view: Record<string, unknown>): Disposable;
    registerToolbarButton(button: Record<string, unknown>): Disposable;
    getContributions(): ApiResult<Array<Record<string, unknown>>>;
    getViews(): ApiResult<Array<Record<string, unknown>>>;
    unregisterView(id: string, ownerId?: string): ApiResult<{ removed: number }>;
    refreshViews(): ApiResult<{ count: number }>;
    renderDeclarativeView(view: Record<string, unknown>): ApiResult;
    renderBottomTabView(view: Record<string, unknown>): ApiResult;
    renderToolbarButton(button: Record<string, unknown>): ApiResult;
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
