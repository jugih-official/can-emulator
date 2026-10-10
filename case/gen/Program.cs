// ---------------------------------------------------------------------------
// The case for the Klemme 15 emulator, as a HelCAD drawing.
//
// Writes, into the folder above this one:
//   can-emulator-case.sihicad.json        the HelCAD drawing: base, lid and the
//                                         boards as blocks, assembled, on layers
//   can-emulator-case-base.stl            the base, as it lies on the printer bed
//   can-emulator-case-lid.stl             the lid, turned outside-down for printing
//   can-emulator-case-base.sihicad.json   the same two, each as a drawing of
//   can-emulator-case-lid.sihicad.json    its own
//
// Everything is in millimetres. X runs across the display, Y from the "lower
// side" -- the wall under the display, where the cable hole is -- to the back,
// Z up. The cavity starts at x = 0, y = 0; the floor's underside is z = 0.
//
// Both parts are made of boxes only: added, or cut away, in the order written
// here. That order is kept in the drawing as each solid's steps, so in HelCAD
// a post or a hook can still be found, moved or taken out. The triangles are
// worked out here, exactly, on the grid the boxes' faces make (see BoxMesh):
// a run of fifty joins through a general boolean drifts by microns, and these
// parts are all right angles.
// ---------------------------------------------------------------------------
using Sihilisko.Cad.Core.Commands;
using Sihilisko.Cad.Core.Editing;
using Sihilisko.Cad.Core.Formats;
using Sihilisko.Cad.Core.Geometry;
using Sihilisko.Cad.Core.Model;
using Sihilisko.Cad.Core.Persistence;

const DrawingUnits Units = DrawingUnits.Millimeters;

// --- the boards, as their drawings give them ---------------------------------
// Arduino Uno R3: a 2700 x 2100 mil board with four 3.2 mm holes. u runs from
// the USB edge along the board, v from the power/analog header edge across it.
const double UnoLength = 68.58, UnoWidth = 53.34, UnoHole = 3.2;
(double U, double V)[] unoHoles = [(13.97, 2.54), (15.24, 50.80), (66.04, 7.62), (66.04, 35.56)];
// What stands tallest on it once wired: a pin socket (8.5), a Dupont plug in
// it (14), and room for the wire to turn (3.4). Solder the wires, or use
// angled plugs, and this number is what brings the whole case down.
const double UnoStack = 8.5 + 14.0 + 3.4;

// MCP2515 + TJA1050 module, the blue one. MEASURE YOURS: sellers give 40 x 28
// and 44 x 28, and the one on the bench looks like 44 in its photograph. It
// is held by its edges, not its holes, so only these two numbers matter.
const double McpLength = 44.0, McpWidth = 28.0;

// 1602A display, from the module drawing: an 80 x 36 x 1.6 board, four 2.5 mm
// holes 75 x 31 apart, a viewing area of 64.5 x 16.4, the frame's front 7.0
// above the board's face, pin 1 of sixteen 8.0 from the left edge.
const double LcdLength = 80.0, LcdWidth = 36.0, LcdHole = 2.5;
const double LcdHolesX = 75.0, LcdHolesY = 31.0;
const double LcdViewX = 64.5, LcdViewY = 16.4;
const double LcdFrameX = 71.3, LcdFrameY = 26.3, LcdFrameHeight = 7.0;
const double Pcb = 1.6;
// The display's wires leave from the back of its board and lie flat against
// it: an angled header or wires soldered on, about this thick.
const double LcdWiring = 3.0;
// A USB-C plug in the Uno's socket: how far its top is above the Uno's board.
const double UsbPlugAbove = 4.85;

// --- the case -----------------------------------------------------------------
const double Wall = 2.0, Floor = 2.0, Lid = 2.0;
const double Standoff = 3.5;                 // under the Uno and the CAN module
const double Fit = 0.3;                      // air around a board
// A hook is a strip that bends: thin, as long as the standoff allows, and
// gripping the board's edge by less than a millimetre. Its lip is two steps,
// the half that meets the board first reaching only half as far: boxes have
// no slopes, and a square lip stops a board dead instead of letting it past.
const double HookThick = 1.2, HookWide = 5.0, HookGrip = 0.7, HookLip = 1.2;

const double Margin = 2.5;                   // board to wall
const double BoardGap = 5.0;                 // Uno to CAN module: two hooks stand in it
// Inside, left to right: the Uno on its end, the CAN module beside it.
double insideX = Math.Ceiling(Margin + UnoWidth + BoardGap + McpWidth + Margin);        // 92
// Front to back: the display over the cable space, then the Uno.
const double LcdY = 5.0;                     // room for the lid's rim and the display's hooks
double unoY = LcdY + LcdWidth + 3.0;         // 44
double insideY = Math.Ceiling((unoY + UnoLength + Margin) * 2) / 2;                     // 115.5
double insideZ = Math.Ceiling(Standoff + Pcb + UnoStack);                               // 31
double top = Floor + insideZ;                // the walls' rim, and the lid's underside

double unoX = Margin;
double mcpX = Margin + UnoWidth + BoardGap;
double mcpY = unoY + 2.0;
double lcdX = (insideX - LcdLength) / 2;
double boardTop = Floor + Standoff + Pcb;

// The one hole: in the lower wall, wide enough for a USB-C plug to pass, with
// the CAN pair and the button's wires beside it.
const double HoleWide = 24.0, HoleHigh = 11.0, HoleUp = 3.0;
// A slot at the back to twist a coin in: the lid sits flush all round.
const double PryWide = 12.0, PryHigh = 1.0;

// Lid: a rim that goes down inside the walls, with a ridge on each long side
// that snaps into a groove in the wall.
const double RimDeep = 5.0, RimThick = 1.6, RimAir = 0.2;
const double Ridge = 0.8, RidgeLong = 10.0, RidgeHigh = 1.4;
const double Groove = 0.9, GrooveLong = 12.0, GrooveHigh = 2.0;

// Where a point of the Uno lands: turned so that its USB edge faces the cable
// hole and its power/analog header is on the CAN module's side.
(double X, double Y) Uno(double u, double v) => (unoX + UnoWidth - v, unoY + u);

// A locating pin, square: its diagonal is what has to pass through the hole.
static double PinSide(double hole) => (hole - 0.5) / Math.Sqrt(2);

// ============================== the base ====================================
var b = new BoxSolid("Case base");
b.Start(-Wall, -Wall, 0, insideX + Wall, insideY + Wall, top);
b.Cut(0, 0, Floor, insideX, insideY, top);
// the cable hole, under the middle of the display
b.Cut(insideX / 2 - HoleWide / 2, -Wall, Floor + HoleUp, insideX / 2 + HoleWide / 2, 0, Floor + HoleUp + HoleHigh);
// the coin slot
b.Cut(insideX / 2 - PryWide / 2, insideY, top - PryHigh, insideX / 2 + PryWide / 2, insideY + Wall, top);

// grooves for the lid's ridges, two in each long wall
double ridgeZ = top - RimDeep + 1.2;
foreach (var y in new[] { insideY * 0.3, insideY * 0.7 })
{
    b.Cut(-Groove, y - GrooveLong / 2, ridgeZ - 0.3, 0, y + GrooveLong / 2, ridgeZ - 0.3 + GrooveHigh);
    b.Cut(insideX, y - GrooveLong / 2, ridgeZ - 0.3, insideX + Groove, y + GrooveLong / 2, ridgeZ - 0.3 + GrooveHigh);
}

// Uno: a post with a pin under each mounting hole, a hook on each long edge
foreach (var (u, v) in unoHoles)
{
    var (x, y) = Uno(u, v);
    double half = PinSide(UnoHole) / 2;
    b.Join(x - 2.5, y - 2.5, Floor, x + 2.5, y + 2.5, Floor + Standoff);
    b.Join(x - half, y - half, Floor + Standoff, x + half, y + half, boardTop + 0.8);
}
double hookTop = boardTop + Fit + HookLip, lipStep = boardTop + Fit + HookLip / 2;
double unoHookY = unoY + 40, unoRight = unoX + UnoWidth;

// A hook standing on the floor at a board's left or right edge: the strip
// outside the edge, and the lip over the board in its two steps.
void FloorHook(double edge, bool boardToTheRight, double y)
{
    double y0 = y - HookWide / 2, y1 = y + HookWide / 2, side = boardToTheRight ? 1 : -1;
    double near = edge - side * Fit, far = near - side * HookThick;
    b.Join(Math.Min(near, far), y0, Floor, Math.Max(near, far), y1, hookTop);
    double grip = edge + side * HookGrip, lead = edge + side * HookGrip / 2;
    b.Join(Math.Min(near, grip), y0, boardTop + Fit, Math.Max(near, grip), y1, lipStep);
    b.Join(Math.Min(near, lead), y0, lipStep, Math.Max(near, lead), y1, hookTop);
}

FloorHook(unoX, true, unoHookY);        // the digital header's side
FloorHook(unoRight, false, unoHookY);   // the power and analog headers' side

// CAN module: a rail under each long edge, a stop at each corner, a hook on each long edge
double mcpRight = mcpX + McpWidth, mcpBack = mcpY + McpLength, mcpMid = mcpY + McpLength / 2;
b.Join(mcpX, mcpMid - 12, Floor, mcpX + 1.5, mcpMid + 12, Floor + Standoff);
b.Join(mcpRight - 1.5, mcpMid - 12, Floor, mcpRight, mcpMid + 12, Floor + Standoff);
foreach (var x in new[] { mcpX, mcpRight - 3.0 })
{
    b.Join(x, mcpY - Fit - 1.5, Floor, x + 3.0, mcpY - Fit, boardTop + 0.5);
    b.Join(x, mcpBack + Fit, Floor, x + 3.0, mcpBack + Fit + 1.5, boardTop + 0.5);
}
FloorHook(mcpX, true, mcpMid);
FloorHook(mcpRight, false, mcpMid);

// =============================== the lid ====================================
double lcdFace = top - LcdFrameHeight;       // the display board's front
double lcdBack = lcdFace - Pcb;
double viewX = lcdX + LcdLength / 2, viewY = LcdY + LcdWidth / 2;

var l = new BoxSolid("Case lid");
l.Start(-Wall, -Wall, top, insideX + Wall, insideY + Wall, top + Lid);
// the window: the display's viewing area and half a millimetre around it
l.Cut(viewX - LcdViewX / 2 - 0.5, viewY - LcdViewY / 2 - 0.5, top, viewX + LcdViewX / 2 + 0.5, viewY + LcdViewY / 2 + 0.5, top + Lid);

// the rim, four strips
double r0 = RimAir, rx1 = insideX - RimAir, ry1 = insideY - RimAir, rimZ = top - RimDeep;
l.Join(r0, r0, rimZ, rx1, r0 + RimThick, top);
l.Join(r0, ry1 - RimThick, rimZ, rx1, ry1, top);
l.Join(r0, r0, rimZ, r0 + RimThick, ry1, top);
l.Join(rx1 - RimThick, r0, rimZ, rx1, ry1, top);
// its ridges, each in two steps like a hook's lip: the lower one, which meets
// the wall's edge first on the way down, stands out half as far
double ridgeStep = ridgeZ + RidgeHigh / 3;
foreach (var y in new[] { insideY * 0.3, insideY * 0.7 })
{
    l.Join(r0 - Ridge / 2, y - RidgeLong / 2, ridgeZ, r0, y + RidgeLong / 2, ridgeStep);
    l.Join(r0 - Ridge, y - RidgeLong / 2, ridgeStep, r0, y + RidgeLong / 2, ridgeZ + RidgeHigh);
    l.Join(rx1, y - RidgeLong / 2, ridgeZ, rx1 + Ridge / 2, y + RidgeLong / 2, ridgeStep);
    l.Join(rx1, y - RidgeLong / 2, ridgeStep, rx1 + Ridge, y + RidgeLong / 2, ridgeZ + RidgeHigh);
}

// display: a post with a pin at each mounting hole
foreach (var hx in new[] { (LcdLength - LcdHolesX) / 2, (LcdLength + LcdHolesX) / 2 })
foreach (var hy in new[] { (LcdWidth - LcdHolesY) / 2, (LcdWidth + LcdHolesY) / 2 })
{
    double x = lcdX + hx, y = LcdY + hy, half = PinSide(LcdHole) / 2;
    l.Join(x - 1.7, y - 1.7, lcdFace, x + 1.7, y + 1.7, top);
    l.Join(x - half, y - half, lcdBack - 0.8, x + half, y + half, lcdFace);
}
// ... and three hooks behind it: two on the edge away from the pins, one beside the pins
double hookEnd = lcdBack - Fit - HookLip, lcdFar = LcdY + LcdWidth, hookX = lcdX + 66;

// A hook hanging from the lid at the display's lower or upper edge. The
// display is pressed in from behind, so the step at the hook's end leads.
void LidHook(double edge, bool boardBehind, double x)
{
    double x0 = x - HookWide / 2, x1 = x + HookWide / 2, side = boardBehind ? 1 : -1;
    double near = edge - side * Fit, far = near - side * HookThick;
    l.Join(x0, Math.Min(near, far), hookEnd, x1, Math.Max(near, far), top);
    double grip = edge + side * HookGrip, lead = edge + side * HookGrip / 2, step = hookEnd + HookLip / 2;
    l.Join(x0, Math.Min(near, lead), hookEnd, x1, Math.Max(near, lead), step);
    l.Join(x0, Math.Min(near, grip), step, x1, Math.Max(near, grip), hookEnd + HookLip);
}

LidHook(LcdY, true, lcdX + 20);
LidHook(LcdY, true, lcdX + 60);
LidHook(lcdFar, false, hookX);

// ===================== the boards, to see that they fit =====================
static CadEntity Block(double x0, double y0, double z0, double x1, double y1, double z1, string layer) =>
    SolidPrimitives.Box(new Point3(x0, y0), new Point3(x1, y1), z1 - z0, layer: layer).Translate(new Vector3(0, 0, z0));

var boards = new List<CadEntity>
{
    Block(unoX, unoY, Floor + Standoff, unoX + UnoWidth, unoY + UnoLength, boardTop, "Boards"),
    Block(mcpX, mcpY, Floor + Standoff, mcpRight, mcpBack, boardTop, "Boards"),
    Block(lcdX, LcdY, lcdBack, lcdX + LcdLength, lcdFar, lcdFace, "Boards"),
    Block(viewX - LcdFrameX / 2, viewY - LcdFrameY / 2, lcdFace, viewX + LcdFrameX / 2, viewY + LcdFrameY / 2, top, "Boards"),
};

// ============================== writing it out ==============================
string outDir = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
var compare = args.Contains("--compare");
foreach (var arg in args.Where(arg => !arg.StartsWith("--"))) outDir = Path.GetFullPath(arg);

var baseSolid = b.Build();
var lidSolid = l.Build();

// What goes through what, and what must not. A board is drawn without its
// holes, so the pins are found inside it and nothing else may be.
void Through(string what, BoxSolid part, double[] block, double expected)
{
    var found = BoxMesh.Shared(part.Boxes, [(SolidStepKind.Base, block)]);
    if (Math.Abs(found - expected) > 1e-6)
    {
        throw new InvalidOperationException($"{what}: {found:0.###} mm3 of the case is inside it, where {expected:0.###} was meant.");
    }
}

double unoPins = 4 * PinSide(UnoHole) * PinSide(UnoHole) * Pcb, lcdPins = 4 * PinSide(LcdHole) * PinSide(LcdHole) * Pcb;
double[] unoBoard = [unoX, unoY, Floor + Standoff, unoX + UnoWidth, unoY + UnoLength, boardTop];
double[] mcpBoard = [mcpX, mcpY, Floor + Standoff, mcpRight, mcpBack, boardTop];
double[] lcdBoard = [lcdX, LcdY, lcdBack, lcdX + LcdLength, lcdFar, lcdFace];
double[] lcdFrame = [viewX - LcdFrameX / 2, viewY - LcdFrameY / 2, lcdFace, viewX + LcdFrameX / 2, viewY + LcdFrameY / 2, top];
// what stands on the Uno and the CAN module, as far up as the lid
double[] unoAbove = [unoX, unoY, boardTop, unoX + UnoWidth, unoY + UnoLength, top];
double[] mcpAbove = [mcpX, mcpY, boardTop, mcpRight, mcpBack, top];
Through("the Uno", b, unoBoard, unoPins);
Through("the CAN module", b, mcpBoard, 0);
Through("the display's board", l, lcdBoard, lcdPins);
Through("the display's frame", l, lcdFrame, 0);
Through("the display's board, in the base", b, lcdBoard, 0);
Through("the Uno, in the lid", l, unoBoard, 0);
// over the boards only the hooks' lips and the pins' ends reach
double lips = HookWide * HookLip / 2 * (HookGrip + HookGrip / 2);
Through("the space over the Uno", b, unoAbove, 2 * lips + 4 * PinSide(UnoHole) * PinSide(UnoHole) * 0.8);
Through("the space over the CAN module", b, mcpAbove, 2 * lips);
Through("the space over the Uno, in the lid", l, unoAbove, 0);
Through("the space over the CAN module, in the lid", l, mcpAbove, 0);
// The USB plug lies under the display's wiring on its way to the Uno.
double plugTop = boardTop + UsbPlugAbove, wiringBottom = lcdBack - LcdWiring;
if (wiringBottom - plugTop < 0.5)
{
    throw new InvalidOperationException(
        $"The USB plug's top is at {plugTop:0.0} mm and the display's wiring comes down to {wiringBottom:0.0}: " +
        "the case is too low for both. Raise UnoStack, or feed the Uno through wires and lower UsbPlugAbove.");
}
if (BoxMesh.Shared(b.Boxes, l.Boxes) > 1e-6)
{
    throw new InvalidOperationException($"The lid and the base overlap by {BoxMesh.Shared(b.Boxes, l.Boxes):0.###} mm3.");
}
Console.WriteLine("fit: the boards clear the case everywhere but at their pins, the lid clears the base, " +
    $"and the USB plug clears the display's wiring by {wiringBottom - plugTop:0.0} mm");

var drawing = new CadDocument("Klemme 15 emulator case");
new AddLayerCommand(new CadLayer("Case base", "#d9d9d9")).Execute(drawing);
new AddLayerCommand(new CadLayer("Case lid", "#f2b84b")).Execute(drawing);
new AddLayerCommand(new CadLayer("Boards", "#3fa7ff")).Execute(drawing);
new AddEntityCommand(baseSolid).Execute(drawing);
new AddEntityCommand(lidSolid).Execute(drawing);
foreach (var board in boards) new AddEntityCommand(board).Execute(drawing);
File.WriteAllText(Path.Combine(outDir, "can-emulator-case.sihicad.json"), CadDocumentJson.Serialize(drawing, indented: false));

// One part as it lies on the printer bed: an STL for the slicer, and a
// drawing of its own for looking at it, or exporting it some other way.
void WriteStl(string name, MeshEntity solid, BoxSolid recipe)
{
    var one = new CadDocument(name);
    new AddLayerCommand(new CadLayer(solid.Layer, drawing.GetLayer(solid.Layer).Color)).Execute(one);
    new AddEntityCommand(solid).Execute(one);
    var stl = BinaryStlWriter.Write(one);                   // refuses anything not watertight
    File.WriteAllBytes(Path.Combine(outDir, name + ".stl"), stl.Content);
    File.WriteAllText(Path.Combine(outDir, name + ".sihicad.json"), CadDocumentJson.Serialize(one, indented: false));
    var report = PrintCheck.Check(solid, Units);
    var size = solid.Bounds;
    Console.WriteLine($"{name}: {recipe.Steps.Count} boxes, {stl.TriangleCount} triangles, " +
        $"{size.Maximum.X - size.Minimum.X:0.0} x {size.Maximum.Y - size.Minimum.Y:0.0} x {size.Maximum.Z - size.Minimum.Z:0.0} mm on the bed, " +
        $"{recipe.Volume / 1000:0.00} cm3, overhanging {report.OverhangShare:P1} of its surface");
    foreach (var issue in report.Issues) Console.WriteLine($"    print check: {issue.Kind}: {issue.Text}");
}

WriteStl("can-emulator-case-base", baseSolid, b);
// The lid prints outside-down, so that its posts, hooks and rim grow upwards.
WriteStl("can-emulator-case-lid", (MeshEntity)PrintCheck.ToBed(PrintCheck.LayFlat(lidSolid, "top")), l);

Console.WriteLine($"inside {insideX} x {insideY} x {insideZ} mm, outside {insideX + 2 * Wall} x {insideY + 2 * Wall} x {top + Lid} mm");
Console.WriteLine($"written to {outDir}");

// With --compare, the same boxes are also put through HelCAD's own boolean,
// one join at a time, to see how far it gets and how close it comes.
if (compare)
{
    foreach (var recipe in new[] { b, l })
    {
        CadEntity built = recipe.Steps[0].Shape;
        var done = 1;
        try
        {
            for (; done < recipe.Steps.Count; done++) built = SolidModel.Continue(built, [recipe.Steps[done]], Units);
            var volume = built is MeshEntity mesh ? BoxMesh.Volume(mesh) : double.NaN;
            Console.WriteLine($"{recipe.Layer}: HelCAD's boolean made all {done} steps; volume {volume:0.######} against {recipe.Volume:0.######}");
        }
        catch (Exception problem)
        {
            Console.WriteLine($"{recipe.Layer}: HelCAD's boolean stopped at step {done} of {recipe.Steps.Count}: {problem.Message}");
        }
    }
}

// ---------------------------------------------------------------------------

/// <summary>A solid said as boxes: one to start from, then boxes added or cut away.</summary>
sealed class BoxSolid(string layer)
{
    private readonly List<(SolidStepKind Kind, double[] Box)> _boxes = [];

    public string Layer => layer;

    /// <summary>The boxes as numbers: x0, y0, z0, x1, y1, z1.</summary>
    public IReadOnlyList<(SolidStepKind Kind, double[] Box)> Boxes => _boxes;

    /// <summary>The same boxes as HelCAD's own steps, which is how the drawing keeps them.</summary>
    public List<SolidStep> Steps { get; } = [];

    /// <summary>Of the finished solid, in cubic millimetres; known once it is built.</summary>
    public double Volume { get; private set; }

    public void Start(double x0, double y0, double z0, double x1, double y1, double z1) =>
        Add(SolidStepKind.Base, x0, y0, z0, x1, y1, z1);

    public void Join(double x0, double y0, double z0, double x1, double y1, double z1) =>
        Add(SolidStepKind.Join, x0, y0, z0, x1, y1, z1);

    public void Cut(double x0, double y0, double z0, double x1, double y1, double z1) =>
        Add(SolidStepKind.Cut, x0, y0, z0, x1, y1, z1);

    private void Add(SolidStepKind kind, double x0, double y0, double z0, double x1, double y1, double z1)
    {
        if (!(x1 > x0 && y1 > y0 && z1 > z0))
        {
            throw new ArgumentException($"Box {_boxes.Count} on {layer} has no size: {x0}..{x1}, {y0}..{y1}, {z0}..{z1}.");
        }
        _boxes.Add((kind, [x0, y0, z0, x1, y1, z1]));
        var shape = SolidPrimitives.Box(new Point3(x0, y0), new Point3(x1, y1), z1 - z0, layer: layer)
            .Translate(new Vector3(0, 0, z0));
        Steps.Add(new SolidStep(kind, shape));
    }

    public MeshEntity Build()
    {
        var (vertices, faces, volume) = BoxMesh.Of(_boxes);
        Volume = volume;
        var mesh = new MeshEntity(EntityId.New(), vertices, faces, layer) { Steps = Steps };
        var meshed = BoxMesh.Volume(mesh);
        if (Math.Abs(meshed - volume) > 1e-6 * volume)
        {
            throw new InvalidOperationException($"{layer}: the triangles enclose {meshed} mm3, the boxes {volume}.");
        }
        return mesh;
    }
}

/// <summary>
/// The surface of boxes added and cut away, worked out exactly.
/// </summary>
/// <remarks>
/// Every face of every box lies on one of a few planes across x, y and z, and
/// those planes cut space into cells. A cell is wholly inside the solid or
/// wholly outside it, by the last box that covers it. The surface is the cell
/// faces with solid on one side only, gathered into the largest rectangles
/// they make, and each rectangle is triangulated with every corner of every
/// neighbour that falls on its outline taken in, so that no triangle ends
/// part way along another's edge. Nothing is rounded anywhere: a corner is
/// three of the boxes' own numbers.
/// </remarks>
static class BoxMesh
{
    /// <summary>The planes the boxes' faces lie on, across each axis.</summary>
    private static double[][] Grid(IEnumerable<(SolidStepKind Kind, double[] Box)> boxes)
    {
        var grid = new double[3][];
        for (var axis = 0; axis < 3; axis++)
        {
            grid[axis] = [.. boxes.SelectMany(box => new[] { box.Box[axis], box.Box[axis + 3] })
                .Select(value => Math.Round(value, 9)).Distinct().Order()];
        }
        return grid;
    }

    /// <summary>Which cells of a grid are solid: each by the last box that covers it.</summary>
    private static bool[,,] Cells(double[][] grid, IEnumerable<(SolidStepKind Kind, double[] Box)> boxes)
    {
        int At(int axis, double value)
        {
            var index = Array.BinarySearch(grid[axis], Math.Round(value, 9));
            return index >= 0 ? index : throw new InvalidOperationException("A box face is not on the grid.");
        }

        var solid = new bool[grid[0].Length - 1, grid[1].Length - 1, grid[2].Length - 1];
        foreach (var (kind, box) in boxes)
        {
            for (var i = At(0, box[0]); i < At(0, box[3]); i++)
            for (var j = At(1, box[1]); j < At(1, box[4]); j++)
            for (var k = At(2, box[2]); k < At(2, box[5]); k++)
            {
                solid[i, j, k] = kind != SolidStepKind.Cut;
            }
        }
        return solid;
    }

    /// <summary>How much two solids have in common, in cubic millimetres.</summary>
    public static double Shared(
        IReadOnlyList<(SolidStepKind Kind, double[] Box)> first,
        IReadOnlyList<(SolidStepKind Kind, double[] Box)> second)
    {
        var grid = Grid(first.Concat(second));
        var a = Cells(grid, first);
        var b = Cells(grid, second);
        var shared = 0.0;
        for (var i = 0; i < grid[0].Length - 1; i++)
        for (var j = 0; j < grid[1].Length - 1; j++)
        for (var k = 0; k < grid[2].Length - 1; k++)
        {
            if (a[i, j, k] && b[i, j, k])
            {
                shared += (grid[0][i + 1] - grid[0][i]) * (grid[1][j + 1] - grid[1][j]) * (grid[2][k + 1] - grid[2][k]);
            }
        }
        return shared;
    }

    public static (List<Point3> Vertices, List<MeshFace> Faces, double Volume) Of(
        IReadOnlyList<(SolidStepKind Kind, double[] Box)> boxes)
    {
        var grid = Grid(boxes);
        int nx = grid[0].Length - 1, ny = grid[1].Length - 1, nz = grid[2].Length - 1;
        var solid = Cells(grid, boxes);

        var volume = 0.0;
        for (var i = 0; i < nx; i++)
        for (var j = 0; j < ny; j++)
        for (var k = 0; k < nz; k++)
        {
            if (solid[i, j, k])
            {
                volume += (grid[0][i + 1] - grid[0][i]) * (grid[1][j + 1] - grid[1][j]) * (grid[2][k + 1] - grid[2][k]);
            }
        }

        bool Solid(int[] cell) =>
            cell[0] >= 0 && cell[0] < nx && cell[1] >= 0 && cell[1] < ny && cell[2] >= 0 && cell[2] < nz &&
            solid[cell[0], cell[1], cell[2]];

        // The rectangles: across each axis, on each plane, facing each way.
        var rectangles = new List<(int Axis, int Plane, bool Up, int U0, int V0, int U1, int V1)>();
        var counts = new[] { nx, ny, nz };
        for (var axis = 0; axis < 3; axis++)
        {
            int ua = (axis + 1) % 3, va = (axis + 2) % 3;
            for (var plane = 0; plane <= counts[axis]; plane++)
            {
                foreach (var up in new[] { true, false })
                {
                    var open = new bool[counts[ua], counts[va]];
                    for (var u = 0; u < counts[ua]; u++)
                    for (var v = 0; v < counts[va]; v++)
                    {
                        var below = new int[3];
                        var above = new int[3];
                        below[axis] = plane - 1; above[axis] = plane;
                        below[ua] = above[ua] = u;
                        below[va] = above[va] = v;
                        open[u, v] = up ? Solid(below) && !Solid(above) : Solid(above) && !Solid(below);
                    }

                    for (var u = 0; u < counts[ua]; u++)
                    for (var v = 0; v < counts[va]; v++)
                    {
                        if (!open[u, v]) continue;
                        var u1 = u + 1;
                        while (u1 < counts[ua] && open[u1, v]) u1++;
                        var v1 = v + 1;
                        for (; v1 < counts[va]; v1++)
                        {
                            var whole = true;
                            for (var uu = u; uu < u1 && whole; uu++) whole = open[uu, v1];
                            if (!whole) break;
                        }
                        for (var uu = u; uu < u1; uu++)
                        for (var vv = v; vv < v1; vv++)
                        {
                            open[uu, vv] = false;
                        }
                        rectangles.Add((axis, plane, up, u, v, u1, v1));
                    }
                }
            }
        }

        // Every rectangle's corners, filed under the grid line each lies on.
        var onLine = new Dictionary<(int Along, int A, int B), SortedSet<int>>();
        void File(int[] corner)
        {
            for (var along = 0; along < 3; along++)
            {
                var key = (along, corner[(along + 1) % 3], corner[(along + 2) % 3]);
                if (!onLine.TryGetValue(key, out var set)) onLine[key] = set = [];
                set.Add(corner[along]);
            }
        }

        int[] Corner(int axis, int plane, int u, int v)
        {
            var corner = new int[3];
            corner[axis] = plane;
            corner[(axis + 1) % 3] = u;
            corner[(axis + 2) % 3] = v;
            return corner;
        }

        foreach (var (axis, plane, _, u0, v0, u1, v1) in rectangles)
        {
            File(Corner(axis, plane, u0, v0));
            File(Corner(axis, plane, u1, v0));
            File(Corner(axis, plane, u1, v1));
            File(Corner(axis, plane, u0, v1));
        }

        var vertices = new List<Point3>();
        var indexOf = new Dictionary<(int, int, int), int>();
        int Index(int[] corner)
        {
            var key = (corner[0], corner[1], corner[2]);
            if (indexOf.TryGetValue(key, out var found)) return found;
            indexOf[key] = vertices.Count;
            vertices.Add(new Point3(grid[0][corner[0]], grid[1][corner[1]], grid[2][corner[2]]));
            return vertices.Count - 1;
        }

        var faces = new List<MeshFace>();
        foreach (var (axis, plane, up, u0, v0, u1, v1) in rectangles)
        {
            int ua = (axis + 1) % 3, va = (axis + 2) % 3;
            // The outline, anticlockwise seen from the plane's upper side, with
            // every filed corner that falls on it.
            var outline = new List<int[]>();
            void Side(int[] from, int[] to)
            {
                var along = from[ua] != to[ua] ? ua : va;
                var set = onLine[(along, from[(along + 1) % 3], from[(along + 2) % 3])];
                var forward = to[along] > from[along];
                var between = set.GetViewBetween(Math.Min(from[along], to[along]), Math.Max(from[along], to[along]));
                foreach (var at in forward ? between : between.Reverse())
                {
                    if (at == to[along]) break;
                    var corner = (int[])from.Clone();
                    corner[along] = at;
                    outline.Add(corner);
                }
            }

            var a = Corner(axis, plane, u0, v0);
            var b = Corner(axis, plane, u1, v0);
            var c = Corner(axis, plane, u1, v1);
            var d = Corner(axis, plane, u0, v1);
            Side(a, b); Side(b, c); Side(c, d); Side(d, a);
            if (!up) outline.Reverse();

            // Ears: three corners in a row that are not all on one side of the
            // rectangle, cut off one at a time. Of those there are, the one
            // whose triangle is least of a sliver.
            var ring = outline.Select(Index).ToList();
            var at3 = outline.ToList();
            while (ring.Count > 3)
            {
                var best = -1;
                var bestShape = -1.0;
                for (var position = 0; position < ring.Count; position++)
                {
                    var previous = at3[(position + ring.Count - 1) % ring.Count];
                    var current = at3[position];
                    var next = at3[(position + 1) % ring.Count];
                    if ((previous[ua] == current[ua] && current[ua] == next[ua]) ||
                        (previous[va] == current[va] && current[va] == next[va])) continue;
                    // Not across a side that still has corners on it: cutting
                    // that ear off would leave them in a row with nothing to
                    // make a triangle of.
                    if ((previous[ua] == next[ua] && (previous[ua] == u0 || previous[ua] == u1)) ||
                        (previous[va] == next[va] && (previous[va] == v0 || previous[va] == v1))) continue;
                    var shape = Shape(vertices[Index(previous)], vertices[Index(current)], vertices[Index(next)]);
                    if (shape > bestShape) { bestShape = shape; best = position; }
                }
                if (best < 0) throw new InvalidOperationException("A rectangle could not be triangulated.");
                faces.Add(new MeshFace(
                    ring[(best + ring.Count - 1) % ring.Count], ring[best], ring[(best + 1) % ring.Count]));
                ring.RemoveAt(best);
                at3.RemoveAt(best);
            }
            faces.Add(new MeshFace(ring[0], ring[1], ring[2]));
        }

        // Every edge between exactly two triangles, or the boxes meet somewhere
        // along an edge only, which no printer can make either.
        var uses = new Dictionary<(int, int), int>();
        foreach (var face in faces)
        {
            foreach (var (from, to) in new[] { (face.A, face.B), (face.B, face.C), (face.C, face.A) })
            {
                var key = from < to ? (from, to) : (to, from);
                uses[key] = uses.GetValueOrDefault(key) + 1;
            }
        }
        foreach (var (edge, count) in uses)
        {
            if (count == 2) continue;
            var p = vertices[edge.Item1];
            var q = vertices[edge.Item2];
            throw new InvalidOperationException(
                $"The boxes leave an edge shared by {count} faces, from ({p.X}, {p.Y}, {p.Z}) to ({q.X}, {q.Y}, {q.Z}): " +
                "two of them touch along an edge without overlapping.");
        }

        return (vertices, faces, volume);
    }

    /// <summary>How far a triangle is from a sliver: its area over its longest side squared.</summary>
    private static double Shape(Point3 a, Point3 b, Point3 c)
    {
        var longest = Math.Max(a.DistanceTo(b), Math.Max(b.DistanceTo(c), c.DistanceTo(a)));
        return (b - a).Cross(c - a).Length / (longest * longest);
    }

    public static double Volume(MeshEntity mesh) => mesh.Faces.Sum(face =>
    {
        var a = mesh.Vertices[face.A] - Point3.Origin;
        var b = mesh.Vertices[face.B] - Point3.Origin;
        var c = mesh.Vertices[face.C] - Point3.Origin;
        return a.Dot(b.Cross(c)) / 6;
    });
}
