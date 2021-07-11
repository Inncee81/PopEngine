//
//  TestGui.swift
//  PopEngineTestApp_Ios
//
//  Created by Graham Reeves on 22/11/2020.
//  Copyright © 2020 NewChromantics. All rights reserved.
//

import SwiftUI

struct ListItem : Identifiable {
    var id = UUID()
    var name: String
}


struct ContentView: View 
{
	@State var label1: String = "Uninitialised"
	@ObservedObject var Label1Wrapper = PopLabel(name:"TestLabel1", label:"InitialLabel")
	@ObservedObject var FrameCounterLabel = PopLabel(name:"FrameCounterLabel", label:"FrameCounter")
	@State var renderView = PopEngineRenderView(name:"TestRenderView") 
    @ObservedObject var TestList = PopList(name: "TestList")
	
	var body: some View 
	{
		OpenglView(renderer:$renderView)
		Text(label1)
		Text(Label1Wrapper.theLabel)
		Text(FrameCounterLabel.theLabel)
		//Text(TestLabel1Wrapper.labelCopy)
        ForEach(TestList.theValue.map( { ListItem(name: $0.value) } ) )
        {
            item in
                Text(verbatim: item.name)
        }
	}			
	/*
	@State var TestLabel1 = PopEngineLabelWrapper(name:"TestLabel1", label:"The Label")
	//var TestLabel1 = PopEngineLabel(name:"TestLabel1", label:"The Label")
	var TestLabel2 = PopEngineLabel(name:"TestLabel2")
	//var TestLabel3 = PopEngineLabel()
	@State var TestButton1 = PopEngineButton(name:"TestButton1x")
	@State var TestButton2 = PopEngineButton(name:"TestButton2")
	//var TestButton3 = PopEngineButton()
	@State var TestTickBox1 = PopEngineTickBox(name:"TestTickBox1")
	var TestTickBox2 = PopEngineTickBox(name:"TestTickBox2", label:"TickBox1")
	var TestTickBox3 = PopEngineTickBox(name:"TestTickBox3", value:true)
	var TestTickBox4 = PopEngineTickBox(name:"TestTickBox4", value:false)
	var TestTickBox5 = PopEngineTickBox(name:"TestTickBox5", value:true, label:"Tickbox5")

	func OnClick2()
	{
		TestButton2.label = "hello";
		TestButton2.onClicked()
	}	

	var body: some View {

		VStack {
			Text(TestLabel1Wrapper.label)
			Text(TestLabel1.label)
			Text(TestLabel2.label )
	        Button(action:TestButton1.onClicked)
	        {
	        	Text(TestButton1.label)
			}
			
			Button(action:OnClick2)
	        {
	        	Text(TestButton2.label)
			}
			
			Toggle("label", isOn: $TestTickBox1.value)
			
			
			Spacer()
		}
	}
	*/
}


struct TestGui_Previews: PreviewProvider {
    static var previews: some View {
		Group {
			ContentView()
				.previewDevice("iPhone SE (2nd generation)")
				.preferredColorScheme(.dark)
		}
    }
}



@main
struct PopEngineTestApp: App {

	var body: some Scene 
	{
		WindowGroup 
		{
			ContentView()
				.onAppear 
				{
					//	call engine startup
					//	gr: this should detect args and load that...
					//		but maybe that should wholly move to the unit test "app" js
					PopEngine("UnitTest/Gui")
				}
		}
	}
}
