import { Stack } from "expo-router";
import { StatusBar } from "expo-status-bar";
import "react-native-reanimated";

import { useColorScheme } from "@/hooks/use-color-scheme";
import HomeScreen from "./screens/HomeScreen";

export default function RootLayout() {

  return (
    <>
      <StatusBar barStyle="light-content" backgroundColor="#2196F3" />
      <HomeScreen />
    </>
  );
}
