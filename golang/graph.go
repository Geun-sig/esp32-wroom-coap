package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"time"

	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/vg"
	vgdraw "gonum.org/v1/plot/vg/draw"
	"gonum.org/v1/plot/vg/vgimg"
)

// timeTicker formats X axis as HH:MM
type timeTicker struct{}

func (timeTicker) Ticks(min, max float64) []plot.Tick {
	if min == max {
		t := time.Unix(int64(min), 0)
		return []plot.Tick{{Value: min, Label: t.Format("15:04")}}
	}
	ticks := make([]plot.Tick, 6)
	step := (max - min) / 5.0
	for i := 0; i <= 5; i++ {
		v := min + float64(i)*step
		ticks[i] = plot.Tick{
			Value: v,
			Label: time.Unix(int64(v), 0).Format("15:04"),
		}
	}
	return ticks
}

func makePlot(pts []DataPoint, title string, lineColor color.Color) *plot.Plot {
	p := plot.New()
	p.Title.Text = title
	p.Title.TextStyle.Color = color.White
	p.BackgroundColor = color.RGBA{R: 30, G: 30, B: 30, A: 255}

	p.X.Color = color.White
	p.Y.Color = color.White
	p.X.Tick.LineStyle.Color = color.White
	p.Y.Tick.LineStyle.Color = color.White
	p.X.Tick.Label.Color = color.White
	p.Y.Tick.Label.Color = color.White
	p.X.Label.TextStyle.Color = color.White
	p.Y.Label.TextStyle.Color = color.White
	p.X.Tick.Marker = timeTicker{}

	if len(pts) > 0 {
		xys := make(plotter.XYs, len(pts))
		for i, pt := range pts {
			xys[i].X = float64(pt.Time.Unix())
			xys[i].Y = pt.Value
		}
		line, err := plotter.NewLine(xys)
		if err == nil {
			line.Color = lineColor
			line.Width = vg.Points(2)
			p.Add(line)
		}
		scatter, err := plotter.NewScatter(xys)
		if err == nil {
			scatter.Color = lineColor
			scatter.Radius = vg.Points(3)
			p.Add(scatter)
		}
	}
	return p
}

func renderOnePlot(pts []DataPoint, title string, lineColor color.Color) image.Image {
	w := vg.Points(620)
	h := vg.Points(190)
	bgColor := color.RGBA{R: 30, G: 30, B: 30, A: 255}
	c := vgimg.NewWith(vgimg.UseWH(w, h), vgimg.UseBackgroundColor(bgColor))
	dc := vgdraw.New(c)
	p := makePlot(pts, title, lineColor)
	p.Draw(dc)
	return c.Image()
}

// RenderHistory renders value and count as stacked subplots.
func RenderHistory(history map[string][]DataPoint, hours int) image.Image {
	img1 := renderOnePlot(history["value"],
		fmt.Sprintf("value  (최근 %dh)", hours),
		color.RGBA{R: 77, G: 171, B: 247, A: 255})
	img2 := renderOnePlot(history["count"],
		fmt.Sprintf("count  (최근 %dh)", hours),
		color.RGBA{R: 105, G: 219, B: 124, A: 255})

	b1 := img1.Bounds()
	b2 := img2.Bounds()
	combined := image.NewRGBA(image.Rect(0, 0, b1.Dx(), b1.Dy()+b2.Dy()))
	draw.Draw(combined, b1, img1, b1.Min, draw.Src)
	draw.Draw(combined, image.Rect(0, b1.Dy(), b2.Dx(), b1.Dy()+b2.Dy()), img2, b2.Min, draw.Src)
	return combined
}
